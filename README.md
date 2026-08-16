<!--
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)
-->

# OBS WhisperBleep

OBS WhisperBleep è un filtro audio nativo per OBS Studio pensato per censurare
in tempo quasi reale parole e frasi configurate dall'utente.

L'idea è semplice: il filtro ascolta l'audio che passa normalmente da OBS,
mantiene un piccolo buffer per avere il tempo di riconoscere le parole con
Whisper e, quando trova una corrispondenza, sostituisce l'intervallo interessato
con un suono scelto dall'utente. Il suono può essere un beep, un "qua qua" da
anatra, un abbaio oppure un file audio personalizzato.

La configurazione è pensata per restare tutta nelle Properties di OBS. Il
plugin dovrà occuparsi del buffering, dei timestamp e del lavoro pesante in
worker separati, così il callback audio di OBS non viene bloccato da download,
accessi al disco o inferenza.

## Nota pratica sulla sincronizzazione

Per mantenere più facilmente la sincronia tra audio e video, si raccomanda di
predisporre tre delay audio consecutivi da **500 msec (0,5 secondi) ciascuno**,
per un ritardo complessivo di circa **1,5 secondi**. La configurazione effettiva
va comunque verificata sulla propria scena e sulla catena OBS utilizzata: il
delay necessario dipende dal modello Whisper, dall'hardware e dalla latenza
complessiva del sistema.

## Stato del progetto

Questo repository parte da uno scaffolding architetturale. Runtime Whisper,
catalogo e download dei modelli, backend GPU, packaging e release multipiattaforma
verranno definiti e verificati nelle milestone del progetto.

## Nota di ispirazione e disclaimer

OBS WhisperBleep nasce da codice originale ed è sviluppato senza effettuare il
fork di CleanStream. Il progetto prende spunto dall'idea di Royshil per
CleanStream; al momento della stesura, il relativo repository Git risultava
abbandonato e non più aggiornato da oltre nove mesi. Non sono riuscito a ottenere
un contatto o una risposta tramite email, messaggi sul repository Git o social.

Questo progetto non dichiara di incorporare codice, commit, asset o pesi di
CleanStream: eventuali componenti di terze parti saranno aggiunti soltanto dopo
la verifica della loro provenienza, licenza e attribution.
