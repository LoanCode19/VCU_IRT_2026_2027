#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "structs.h"
#include "queueHandles.h"
#include "utils.h"
#include "TaskCoolingSystem.h"

#ifndef COOLING_RX_TIMEOUT_MS
#define COOLING_RX_TIMEOUT_MS 100U
#endif

#ifndef COOLING_TX_TIMEOUT_MS
#define COOLING_TX_TIMEOUT_MS 5U
#endif

#ifndef COOLING_TEMP_TIMEOUT_MS
#define COOLING_TEMP_TIMEOUT_MS 500U
#endif

#ifndef COOLING_PUMP_ON_TEMP_C
#define COOLING_PUMP_ON_TEMP_C 50
#endif

#ifndef COOLING_PUMP_OFF_TEMP_C
#define COOLING_PUMP_OFF_TEMP_C 45
#endif

#ifndef COOLING_FANS_ON_TEMP_C
#define COOLING_FANS_ON_TEMP_C 55
#endif

#ifndef COOLING_FANS_OFF_TEMP_C
#define COOLING_FANS_OFF_TEMP_C 50
#endif

typedef struct
{
	dataMessage_t motorTemp;
	dataMessage_t inverterTemp;
	bool pumpEnabled;
	bool fansEnabled;
	bool hasPublishedOrders;
	bool lastPublishedPumpEnabled;
	bool lastPublishedFansEnabled;
} CoolingState_t;

static bool sPumpEnabled = false;
static bool sFansEnabled = false;

static bool ApplyOnOffHysteresis(bool currentlyEnabled, int32_t measuredTemp, int32_t onThreshold, int32_t offThreshold)
{
	if (currentlyEnabled)
	{
		return (measuredTemp > offThreshold);
	}

	return (measuredTemp >= onThreshold);
}

static void ToggleCoolingPumps(bool state)
{
	sPumpEnabled = state;
}

static void ToggleCoolingFans(bool state)
{
	sFansEnabled = state;
}

static void PublishCoolingOrder(Order_t order, bool state)
{
	DigitalOrder_t outputOrder = {0};

	if (xDSQueue == NULL)
	{
		return;
	}

	outputOrder.order = order;
	outputOrder.state = state;
	(void)xQueueSend(xDSQueue, &outputOrder, pdMS_TO_TICKS(COOLING_TX_TIMEOUT_MS));
}

static void PublishCoolingOrdersIfChanged(CoolingState_t *state, bool pumpEnabled, bool fansEnabled)
{
	bool publishPump = (!state->hasPublishedOrders) ||
					   (pumpEnabled != state->lastPublishedPumpEnabled);

	bool publishFans = (!state->hasPublishedOrders) ||
					   (fansEnabled != state->lastPublishedFansEnabled);

	if (publishPump)
	{
		PublishCoolingOrder(CoolingPump, pumpEnabled);
	}

	if (publishFans)
	{
		PublishCoolingOrder(CoolingFans, fansEnabled);
	}

	state->hasPublishedOrders = true;
	state->lastPublishedPumpEnabled = pumpEnabled;
	state->lastPublishedFansEnabled = fansEnabled;
}

static void RefreshTemperatureStatuses(CoolingState_t *state, TickType_t now)
{
	updateDataTimeStatus(&state->motorTemp, 1, COOLING_TEMP_TIMEOUT_MS, now);
	updateDataTimeStatus(&state->inverterTemp, 1, COOLING_TEMP_TIMEOUT_MS, now);
}

static void EvaluateCooling(CoolingState_t *state)
{
	bool motorValid = (state->motorTemp.timeStatus == STATUS_TIME_OK);
	bool inverterValid = (state->inverterTemp.timeStatus == STATUS_TIME_OK);
	int32_t hottestTemp = 0;

	if ((!motorValid) && (!inverterValid))
	{
		bool hasTimedOut = (state->motorTemp.timeStatus == STATUS_TIMEOUT) ||
						   (state->inverterTemp.timeStatus == STATUS_TIMEOUT);

		state->pumpEnabled = hasTimedOut;
		state->fansEnabled = hasTimedOut;

		ToggleCoolingPumps(state->pumpEnabled);
		ToggleCoolingFans(state->fansEnabled);
		return;
	}

	hottestTemp = motorValid ? state->motorTemp.data : state->inverterTemp.data;
	if (inverterValid && ((!motorValid) || (state->inverterTemp.data > hottestTemp)))
	{
		hottestTemp = state->inverterTemp.data;
	}

	state->pumpEnabled = ApplyOnOffHysteresis(
		state->pumpEnabled,
		hottestTemp,
		COOLING_PUMP_ON_TEMP_C,
		COOLING_PUMP_OFF_TEMP_C);

	state->fansEnabled = ApplyOnOffHysteresis(
		state->fansEnabled,
		hottestTemp,
		COOLING_FANS_ON_TEMP_C,
		COOLING_FANS_OFF_TEMP_C);

	ToggleCoolingPumps(state->pumpEnabled);
	ToggleCoolingFans(state->fansEnabled);
}

void TaskCoolingSystem(void *pvParameters)
{
	CoolingParams_t *params = (CoolingParams_t *)pvParameters;
	QueueHandle_t *xTCSQueue = params->xCoolingQueue;
	QueueHandle_t *xWDGQueue = params->xWDGQueue;
	TaskId taskId = params->taskId;
	(void)xWDGQueue; /* used only when USE_WDG is defined */
	(void)taskId;
	dataMessage_t incomingMsg = {0};
	CoolingState_t state = {0};

	state.motorTemp.id = motor_tempId;
	state.inverterTemp.id = inverter_tempId;
	state.motorTemp.timeStatus = STATUS_NOT_READY;
	state.inverterTemp.timeStatus = STATUS_NOT_READY;

	if ((xTCSQueue == NULL) || (*xTCSQueue == NULL))
	{
		// vTaskDelete(NULL);
	}

	for (;;)
	{
		bool hasNewMeasurement = false;
		DataTimeStatus_t previousMotorStatus = state.motorTemp.timeStatus;
		DataTimeStatus_t previousInverterStatus = state.inverterTemp.timeStatus;

		if (xQueueReceive(*xTCSQueue, &incomingMsg, pdMS_TO_TICKS(COOLING_RX_TIMEOUT_MS)) == pdPASS)
		{
			if (incomingMsg.id == motor_tempId)
			{
				state.motorTemp = incomingMsg;
				state.motorTemp.timeStatus = STATUS_TIME_OK;
				hasNewMeasurement = true;
			}
			else if (incomingMsg.id == inverter_tempId)
			{
				state.inverterTemp = incomingMsg;
				state.inverterTemp.timeStatus = STATUS_TIME_OK;
				hasNewMeasurement = true;
			}
		}

		RefreshTemperatureStatuses(&state, xTaskGetTickCount());

		if (hasNewMeasurement ||
			(previousMotorStatus != state.motorTemp.timeStatus) ||
			(previousInverterStatus != state.inverterTemp.timeStatus))
		{
			EvaluateCooling(&state);
			PublishCoolingOrdersIfChanged(
				&state,
				sPumpEnabled,
				sFansEnabled);
		}

#ifdef USE_WDG
		wdgFeed(taskId, xTaskGetTickCount(), xWDGQueue);
#endif
	}
}
