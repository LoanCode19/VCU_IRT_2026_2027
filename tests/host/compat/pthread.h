#ifndef TEST_HOST_COMPAT_PTHREAD_H
#define TEST_HOST_COMPAT_PTHREAD_H

// src/VCU.c inclut <pthread.h> sans l'utiliser (probleme R6 de AUDIT_VCU.md).
// MinGW n'en fournit pas : ce fichier vide permet de compiler VCU.c sur PC.
// Ce dossier n'est ajoute au chemin d'inclusion que si le compilateur n'a pas pthread.h.

#endif // TEST_HOST_COMPAT_PTHREAD_H
