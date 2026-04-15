#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "structs.h"
#include "queueHandles.h"
#include "TaskCoolingSystem.h"

#ifndef COOLING_RX_TIMEOUT_MS
#define COOLING_RX_TIMEOUT_MS 100U
#endif

#ifndef COOLING_TX_TIMEOUT_MS
#define COOLING_TX_TIMEOUT_MS 5U
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

#ifndef COOLING_PUMP_MIN_LEVEL_PERCENT
#define COOLING_PUMP_MIN_LEVEL_PERCENT 40U
#endif

#ifndef COOLING_FANS_MIN_LEVEL_PERCENT
#define COOLING_FANS_MIN_LEVEL_PERCENT 40U
#endif

int32_t maxTemp;

typedef struct
{
	int32_t motorTemp;
	int32_t inverterTemp;
	bool hasMotorTemp;
	bool hasInverterTemp;
	bool pumpEnabled;
	bool fansEnabled;
	uint8_t pumpLevelPercent;
	uint8_t fansLevelPercent;
} CoolingState_t;

static bool sPumpEnabled = false;
static bool sFansEnabled = false;
static uint8_t sPumpLevelPercent = 0U;
static uint8_t sFansLevelPercent = 0U;

static uint8_t ComputePumpLevelPercent(int32_t hottestTemp)
{
	if (hottestTemp >= 70)
	{
		return 100U;
	}

	if (hottestTemp >= 60)
	{
		return 70U;
	}

	if (hottestTemp >= 50)
	{
		return 40U;
	}

	return 0U;
}

static uint8_t ComputeFansLevelPercent(int32_t hottestTemp)
{
	if (hottestTemp >= 75)
	{
		return 100U;
	}

	if (hottestTemp >= 65)
	{
		return 70U;
	}

	if (hottestTemp >= 55)
	{
		return 40U;
	}

	return 0U;
}

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

static void SetCoolingPumpsLevel(uint8_t levelPercent)
{
	sPumpLevelPercent = levelPercent;
}

static void SetCoolingFansLevel(uint8_t levelPercent)
{
	sFansLevelPercent = levelPercent;
}

static void PublishCoolingOrder(Order_t order, bool state, uint8_t levelPercent)
{
	DigitalOrder_t outputOrder = {0};

	if (xDSQueue == NULL)
	{
		return;
	}

	outputOrder.order = order;
	outputOrder.state = state;
	outputOrder.levelPercent = levelPercent;
	(void)xQueueSend(xDSQueue, &outputOrder, pdMS_TO_TICKS(COOLING_TX_TIMEOUT_MS));
}

static void EvaluateCooling(CoolingState_t *state)
{
	int32_t hottestTemp = state->motorTemp;
	uint8_t requestedPumpLevel = 0U;
	uint8_t requestedFansLevel = 0U;

	if ((!state->hasMotorTemp) && (!state->hasInverterTemp))
	{
		return;
	}

	if (state->hasInverterTemp && ((!state->hasMotorTemp) || (state->inverterTemp > hottestTemp)))
	{
		hottestTemp = state->inverterTemp;
	}

	requestedPumpLevel = ComputePumpLevelPercent(hottestTemp);
	requestedFansLevel = ComputeFansLevelPercent(hottestTemp);

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

	if (state->pumpEnabled)
	{
		state->pumpLevelPercent = requestedPumpLevel;
		if (state->pumpLevelPercent < COOLING_PUMP_MIN_LEVEL_PERCENT)
		{
			state->pumpLevelPercent = COOLING_PUMP_MIN_LEVEL_PERCENT;
		}
	}
	else
	{
		state->pumpLevelPercent = 0U;
	}

	if (state->fansEnabled)
	{
		state->fansLevelPercent = requestedFansLevel;
		if (state->fansLevelPercent < COOLING_FANS_MIN_LEVEL_PERCENT)
		{
			state->fansLevelPercent = COOLING_FANS_MIN_LEVEL_PERCENT;
		}
	}
	else
	{
		state->fansLevelPercent = 0U;
	}

	SetCoolingPumpsLevel(state->pumpLevelPercent);
	ToggleCoolingPumps(state->pumpEnabled);
	ToggleCoolingFans(state->fansEnabled);
	SetCoolingFansLevel(state->fansLevelPercent);
}

void TaskCoolingSystem(void *pvParameters)
{
	QueueHandle_t *xTCSQueue = (QueueHandle_t *)pvParameters;
	dataMessage_t incomingMsg = {0};
	CoolingState_t state = {0};

	if ((xTCSQueue == NULL) || (*xTCSQueue == NULL))
	{
		vTaskDelete(NULL);
	}

	for (;;)
	{
		if (xQueueReceive(*xTCSQueue, &incomingMsg, pdMS_TO_TICKS(COOLING_RX_TIMEOUT_MS)) == pdPASS)
		{
			bool shouldEvaluate = false;

			if (incomingMsg.id == motor_tempId)
			{
				state.motorTemp = incomingMsg.data;
				state.hasMotorTemp = true;
				shouldEvaluate = true;
			}
			else if (incomingMsg.id == inverter_tempId)
			{
				state.inverterTemp = incomingMsg.data;
				state.hasInverterTemp = true;
				shouldEvaluate = true;
			}

			if (shouldEvaluate)
			{
				EvaluateCooling(&state);
				PublishCoolingOrder(CoolingPump, sPumpEnabled, sPumpLevelPercent);
				PublishCoolingOrder(CoolingFans, sFansEnabled, sFansLevelPercent);
			}
		}
		(void)sPumpEnabled;
		(void)sFansEnabled;
		(void)sPumpLevelPercent;
		(void)sFansLevelPercent;
	}
}
