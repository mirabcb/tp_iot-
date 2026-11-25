# ❤️ Moniteur cardiaque connecté : ESP32, I2C, MQTT et Node-RED

**Mini-projet Systèmes Embarqués Biomédicaux, ISIS 2026 · Partie Infrastructure**
**Thème choisi : douleurs thoraciques**
<img width="2048" height="1181" alt="d59b3a7c-1e52-489d-b587-3232299f3441" src="https://github.com/user-attachments/assets/cffc0972-ebec-4b97-81a6-4f82387f5577" />

<img width="1200" height="1600" alt="WhatsApp Image 2026-11-25 at 14 13 09" src="https://github.com/user-attachments/assets/5551911e-593f-4e2d-9853-9d13a2674227" />

Ce prototype mesure en continu la **fréquence cardiaque (BPM)** et la **saturation en oxygène (SpO2)** d'un patient avec un capteur optique posé sur le doigt. Les mesures sont envoyées sans fil, par **MQTT**, à un **Raspberry Pi** qui les affiche en temps réel sur un **dashboard Node-RED**.

Quand le BPM dépasse un seuil réglable, ou en cas de panne (capteur débranché, nœud hors ligne), **une alarme se déclenche**. Elle se manifeste de trois façons : une LED s'allume sur un second ESP32, une popup apparaît et une sirène sonne sur le dashboard. Un **bouton physique** permet d'acquitter l'alarme. Un troisième ESP32, relié en **I2C**, joue le rôle de **valise de diagnostic**.

---

## Sommaire
1. [Project overview](#1-project-overview)
2. [Prérequis](#2-prérequis)
3. [Composants nécessaires (parts required)](#3-composants-nécessaires-parts-required)
4. [Schémas électriques](#4-schémas-électriques)
5. [Fonctionnement de chaque nœud](#5-fonctionnement-de-chaque-nœud)
6. [Communication MQTT](#6-communication-mqtt)
7. [Flow Node-RED et dashboard](#7-flow-node-red-et-dashboard)
8. [Explications sur le code](#8-explications-sur-le-code)
9. [Démonstration et tests](#9-démonstration-et-tests)
10. [Difficultés rencontrées et solutions](#10-difficultés-rencontrées-et-solutions)
11. [Améliorations et perspectives](#11-améliorations-et-perspectives)
12. [Références](#12-références)


## 1. Project overview

Architecture du système :



<img width="1655" height="1072" alt="shem drawio" src="https://github.com/user-attachments/assets/480ea251-d577-46e8-8fd0-f87dfb970a3c" />

Le système est composé de **5 éléments** :

| Élément | Rôle | Liaisons |
|---|---|---|
| **ESP32 n°1 : nœud principal** | Lit le capteur cardiaque, publie BPM et SpO2 en JSON, calcule un code d'erreur | I2C (capteur) · I2C (diagnostic) · WiFi/MQTT |
| **ESP32 n°2 : nœud de diagnostic** | Interroge le nœud principal toutes les 2 s et affiche son état sur le Serial Monitor | I2C · USB vers le PC |
| **ESP32 n°3 : nœud de pilotage** | LED commandée à distance, bouton qui envoie son état | WiFi/MQTT |
| **Raspberry Pi 4** | Broker MQTT (Mosquitto), Node-RED et dashboard, logique d'alarme | WiFi · HTTP · SSH |
| **PC** | Affiche le dashboard, programme les ESP32, lit le diagnostic | HTTP · SSH · USB |

### Principe général
1. Le **nœud principal** lit le capteur toutes les 4 s et publie un message JSON sur `projet/principal/data`.
2. **Node-RED** reçoit ce message : il trace les courbes, met à jour la jauge et exécute la **logique d'alarme**.
3. Si le BPM dépasse le seuil, Node-RED publie `on` sur `esp32/output` : la **LED du nœud de pilotage s'allume**, une popup s'affiche et une sirène sonne sur le dashboard.
4. Un **appui court sur le bouton** du nœud de pilotage acquitte l'alarme. Un **appui long** démarre ou arrête l'enregistrement des mesures.
5. En parallèle, le **nœud de diagnostic** lit en I2C le code d'erreur du nœud principal et l'affiche en clair : « Aucune erreur », « Perte du Wifi », « Capteur non connecte »…

**Choix d'architecture :** toute l'**intelligence** (seuil, alarme, acquittement, enregistrement) est **centralisée dans Node-RED**. Les ESP32 restent simples : l'un **mesure**, l'autre **exécute**. On peut ainsi modifier le seuil ou la logique depuis le dashboard sans reprogrammer aucune carte.


## 2. Prérequis

Avant de reproduire ce projet, il faut avoir installé :

| Outil | Tutoriel |
|---|---|
| Carte ESP32 dans l'Arduino IDE | [Installing the ESP32 Board in Arduino IDE (Random Nerd Tutorials)](https://randomnerdtutorials.com/installing-the-esp32-board-in-arduino-ide-windows-instructions/) |
| Broker MQTT Mosquitto sur Raspberry Pi | [Install Mosquitto Broker on Raspberry Pi (Random Nerd Tutorials)](https://randomnerdtutorials.com/how-to-install-mosquitto-broker-on-raspberry-pi/) |
| Node-RED sur Raspberry Pi | [Running Node-RED on a Raspberry Pi (nodered.org)](https://nodered.org/docs/getting-started/raspberrypi) |
| Dashboard Node-RED | [node-red-dashboard](https://flows.nodered.org/node/node-red-dashboard) (Menu ☰ → Gérer la palette → Installer) |
| Exemple ESP32 + MQTT | [ESP32 MQTT Publish Subscribe (Random Nerd Tutorials)](https://randomnerdtutorials.com/esp32-mqtt-publish-subscribe-arduino-ide/) |
| Base du TP | [AlCoISIS/TpMqttNodeRed (wiki du cours)](https://github.com/AlCoISIS/TpMqttNodeRed/wiki) |

**Bibliothèques Arduino** (Croquis → Inclure une bibliothèque → Gérer les bibliothèques) :
- **PubSubClient** (Nick O'Leary) : client MQTT
- **DFRobot_BloodOxygen_S** (DFRobot) : capteur SEN0518

## 3. Composants nécessaires 

| Qté | Composant | Référence |
|---|---|---|
| 3 | Carte ESP32 | DFRobot **FireBeetle ESP32** |
| 1 | Capteur de fréquence cardiaque et d'oxymétrie | DFRobot **Gravity SEN0518** (MAX30102, I2C, adresse 0x57) |
| 1 | Raspberry Pi 4 + carte SD + alimentation | Raspberry Pi OS, IP 172.18.32.39 |
| 1 | LED rouge 5 mm | |
| 1 | Résistance 220 Ω | pour la LED |
| 1 | Bouton poussoir | |
| 2 | Breadboards | |
| ~15 | Fils Dupont mâle/mâle et mâle/femelle | |
| 3 | Câbles micro-USB | alimentation et programmation des ESP32 |
| 1 | PC | Arduino IDE + navigateur |

## 4. Schémas électriques

### Nœud principal ↔ capteur (bus I2C n°1, le principal est maître)
| Capteur SEN0518 | ESP32 principal |
|---|---|
| VCC (rouge) | 3V3 |
| GND (noir) | GND |
| D/T = SDA (vert) | **GPIO25** |
| C/T = SCL (bleu) | **GPIO26** |

⚠️ Le sélecteur du module doit être sur **I2C** (pas UART).

### Nœud principal ↔ nœud de diagnostic (bus I2C n°0, le principal est esclave 0x55)
| ESP32 principal | ESP32 diagnostic |
|---|---|
| **GPIO21** (SDA) | **GPIO21** (SDA) |
| **GPIO22** (SCL) | **GPIO22** (SCL) |
| **GND** | **GND** (masse commune obligatoire) |

### Nœud de pilotage
| Composant | Branchement |
|---|---|
| LED rouge | GPIO26 → résistance 220 Ω → LED (+) ; LED (−) → GND |
| Bouton poussoir | GPIO25 → bouton → GND (pull-up interne activé dans le code) |

**Pourquoi deux bus I2C sur le nœud principal ?** L'ESP32 possède **deux contrôleurs I2C**. Le nœud principal est **maître** sur le bus 1 pour lire le capteur, et **esclave** sur le bus 0 pour répondre au diagnostic. Les deux rôles ne peuvent pas cohabiter sur les mêmes fils. Nous l'avons d'abord essayé, et ça ne fonctionnait pas .

<img width="1200" height="1600" alt="WhatsApp Image 2026-11-25 at 14 19 34" src="https://github.com/user-attachments/assets/1dbab5e5-10ec-46ed-9330-755409c012bc" />

<img width="1200" height="1600" alt="WhatsApp Image 2026-11-25 at 14 18 18" src="https://github.com/user-attachments/assets/10b9330c-d261-49f6-83e5-5dbf6d8126a8" />

## 5. Fonctionnement de chaque nœud

### 5.1 Nœud principal
- Toutes les **secondes**, il vérifie que le capteur répond sur le bus I2C. S'il ne répond pas : `capteurOK = false` et code d'erreur **2**.
- Si le capteur est rebranché, il est **réinitialisé automatiquement**, sans redémarrer l'ESP32.
- Toutes les **4 secondes**, il lit BPM, SpO2 et la température de la carte, calcule le code d'erreur et publie un JSON sur MQTT.
- Le WiFi et le MQTT sont gérés **sans bloquer le programme** : si le WiFi tombe, l'ESP32 continue de lire le capteur et de répondre au diagnostic, et envoie le code **1** (Perte du Wifi).
- Il s'annonce `online` au broker et laisse un **Last Will** `offline` : si l'ESP32 disparaît, le broker prévient Node-RED automatiquement.

### 5.2 Nœud de diagnostic (« valise de diagnostic »)
Toutes les 2 s, il demande **1 octet** (le code d'erreur) au nœud principal et affiche sa signification :

| Code | Message affiché | Situation de test |
|---|---|---|
| 0 | Aucune erreur | Doigt posé, mesure valide |
| 1 | Perte du Wifi | On coupe le routeur WiFi |
| 2 | Capteur non connecte | On débranche un fil du capteur |
| 3 | Erreur de lecture du capteur | Capteur branché mais pas de doigt (BPM = -1) |
| (aucune réponse) | Erreur : aucune reponse du Slave | On débranche la liaison I2C ou on éteint le principal |


<img width="603" height="746" alt="errur 0" src="https://github.com/user-attachments/assets/781ccc42-0638-4793-a0e9-bb419e45b19c" />

### 5.3 Nœud de pilotage
- La **LED** est commandée **uniquement par MQTT**, sur le topic `esp32/output` (`on` / `off`). Elle obéit à la fois à l'interrupteur du dashboard et à l'alarme de Node-RED.
- Le **bouton** publie `down` (appuyé) ou `up` (relâché) sur `esp32/button1`, avec un anti-rebond de 30 ms. Il ne commande pas la LED directement : c'est Node-RED qui décide.

---

## 6. Communication MQTT

Broker : **Mosquitto** sur le Raspberry Pi, `172.18.32.39:1883`.

| Topic | Émetteur → récepteur | Contenu |
|---|---|---|
| `projet/principal/data` | Principal → Node-RED | JSON des mesures (toutes les 4 s) |
| `projet/principal/bpm`, `spo2`, `code` | Principal → Node-RED | valeurs seules |
| `projet/principal/online` | Principal (ou broker) → Node-RED | `online` / `offline` (Last Will) |
| `projet/principal/seuil` | Node-RED → Principal | seuil BPM réglé sur le dashboard |
| `esp32/output` | Node-RED → Pilotage | `on` / `off` |
| `esp32/button1` | Pilotage → Node-RED | `down` / `up` |

**Exemple de message JSON** (amélioration demandée : données au format JSON) :
```json
{"bpm":78,"spo2":98,"temp":26.0,"doigt":true,"capteur":true,"code":0,"seuil":100}
```
---

## 7. Flow Node-RED et dashboard

<img width="1200" height="1600" alt="WhatsApp Image 2026-11-25 at 14 31 41" src="https://github.com/user-attachments/assets/9c7b5e67-8d00-488b-b6e5-1ef31b327e49" />
<img width="1600" height="1200" alt="WhatsApp Image 2026-11-25 at 14 31 48" src="https://github.com/user-attachments/assets/dff4b898-12bb-46eb-88fb-ca52fa74703d" />
<img width="1200" height="1600" alt="WhatsApp Image 2026-11-25 at 14 37 38" src="https://github.com/user-attachments/assets/d52c05b6-4a05-4895-8a6b-ceb83080b113" />
<img width="1200" height="1600" alt="WhatsApp Image 2026-11-25 at 14 37 37" src="https://github.com/user-attachments/assets/968c0f47-eb07-4e16-ae11-c32c40d7b398" />


Le flow (`node-red/flow_miniprojet_cardio.json`) est organisé en 4 parties.

**1. Affichage des mesures (groupe Patient)**
`mqtt in projet/principal/data` → fonction « Extraire BPM / SpO2 » → jauge BPM + graphique BPM + graphique SpO2 (5 dernières minutes). Les mesures invalides (-1) sont ignorées. On affiche aussi l'état du capteur et si le nœud principal est en ligne.

**2. Pilotage (groupe Pilotage)**
- Interrupteur **LED** → commande la LED du nœud de pilotage.
- **État du bouton physique** : APPUYÉ / RELÂCHÉ.

**3. Logique d'alarme (groupe Alarme)**
La fonction **« Logique d'alarme »** reçoit les mesures, l'état en ligne, le bouton, le seuil (curseur), l'interrupteur « Surveillance » et le bouton « Acquitter ». Elle déclenche l'alarme si :
- le **BPM dépasse le seuil** ;
- le **capteur est débranché** ;
- le **nœud principal est hors ligne**.

Quand l'alarme se déclenche :
- la **LED s'allume** (`on` sur `esp32/output`) ;
- une **popup rouge** apparaît ;
- l'**état** s'affiche en rouge ;
- une **sirène** sonne, accompagnée d'une **voix** (« Alerte, fréquence cardiaque trop élevée… »).

**Prise en compte du bouton** (exigence de la consigne) : un **appui court** acquitte l'alarme, ce qui éteint la LED et coupe la sirène. L'alarme se **réarme automatiquement** quand le problème disparaît, ou quand une autre cause apparaît.

**4. Enregistrement des données**
Un **appui long (≥ 1,5 s)** sur le bouton physique démarre ou arrête l'enregistrement. Les mesures valides sont stockées dans Node-RED et **téléchargeables en CSV** depuis le dashboard (lien « 📥 Télécharger les mesures »), ou directement à l'adresse `http://172.18.32.39:1880/cardio.csv`.

---

## 8.  le code :

Le code complet et commenté se trouve dans le dossier : https://github.com/mirabcb/tp_iot-.git .


## 9. Démonstration et tests

| # | Action | Résultat attendu | ✔ |
|---|---|---|---|
| 1 | Doigt posé sur le capteur | Courbes BPM et SpO2 et jauge sur le dashboard ; diagnostic : « Aucune erreur » | |
| 2 | Interrupteur LED sur le dashboard | La LED du nœud de pilotage s'allume / s'éteint | |
| 3 | Appui sur le bouton physique | « APPUYÉ / RELÂCHÉ » sur le dashboard | |
| 4 | Seuil réglé sous le BPM actuel (ex. 60) | LED allumée + popup + sirène + voix | |
| 5 | Appui court sur le bouton | Alarme acquittée : LED éteinte, sirène coupée | |
| 6 | Débrancher un fil du capteur | Alarme « Capteur débranché » ; diagnostic : code 2 « Capteur non connecte » | |
| 7 | Rebrancher le capteur | Retour automatique à la normale | |
| 8 | Couper le routeur WiFi | Diagnostic : code 1 « Perte du Wifi » ; dashboard : principal HORS LIGNE | |
| 9 | Débrancher la liaison I2C | Diagnostic : « aucune reponse du Slave » | |
| 10 | Appui long sur le bouton, puis téléchargement CSV | Fichier CSV avec date, BPM, SpO2 | |

> 🎥 **À AJOUTER (facultatif) : courte vidéo ou GIF de la démonstration**

---

## 10. Difficultés rencontrées et solutions

| Problème | Cause | Solution |
|---|---|---|
| `init fail!` en boucle sur le capteur | Capteur câblé sur d'autres broches que celles du code | Faire correspondre le câblage au code : capteur sur le bus 1 (GPIO25/26) |
| Le diagnostic ne recevait rien quand le capteur était absent | `while (!MAX30102.begin())` bloquait tout le programme avant le démarrage de l'esclave I2C | Esclave démarré en premier + détection non bloquante du capteur |
| Capteur et diagnostic branchés en parallèle sur les mêmes SDA/SCL | Un même ESP32 ne peut pas être maître et esclave sur les mêmes fils | Deux bus séparés : `Wire` (esclave) et `TwoWire(1)` (maître) |
| Le scanner I2C du Master trouvait 0x57 au lieu de 0x55 | Le Master était relié aux broches du capteur | Rebranchement : Master ↔ bus 0 du principal uniquement |
| Bouton et LED sur la même broche (GPIO25) en fusionnant les codes du TP | Les deux codes du TP utilisaient la même broche | Bouton sur GPIO25, LED sur GPIO26 |
| Le bouton allumait la LED directement | Code local qui court-circuitait Node-RED | LED commandée uniquement par MQTT |
| Nœuds « unknown » et dashboard bloqué sur « Loading » | Mélange de Dashboard 2.0 et de l'ancien node-red-dashboard + imports en double | Nettoyage complet des flows et des nœuds de configuration, un seul dashboard |
| BPM = -1 | Doigt mal posé ou qui bouge | Bracelet serré, doigt immobile pendant 20 à 30 s |

---

## 11. Améliorations et perspectives

**Améliorations réalisées (au-delà du minimum) :**
- ✅ Transfert des données au **format JSON** via MQTT
- ✅ Mesure de la **SpO2** en plus du BPM
- ✅ **Seuil réglable** depuis le dashboard et renvoyé à l'ESP32
- ✅ **Last Will MQTT** : détection automatique d'un nœud hors ligne
- ✅ Alarme multi-causes (BPM, capteur débranché, nœud hors ligne) avec **acquittement** et réarmement automatique
- ✅ **Sirène et voix** sur le dashboard
- ✅ **Enregistrement des mesures** par appui long et **export CSV**
- ✅ Détection et **reconnexion à chaud** du capteur

**Pistes :**
- Alarme locale (buzzer) sur le nœud principal, qui fonctionne même sans réseau
- Historisation dans **InfluxDB + Grafana**
- Notification par e-mail ou Telegram en cas d'alarme

---


## 12. Références

- DFRobot, *Gravity: MAX30102 PPG Heart Rate and Oximeter Sensor (SEN0518)*, [wiki DFRobot](https://wiki.dfrobot.com/SKU_SEN0518_Gravity_MAX30102_Heart_Rate_and_Oximeter_Sensor)
- DFRobot, bibliothèque [DFRobot_BloodOxygen_S](https://github.com/DFRobot/DFRobot_BloodOxygen_S)
- A. Combes, [TpMqttNodeRed : wiki du cours](https://github.com/AlCoISIS/TpMqttNodeRed/wiki) (I2C maître/esclave, capteur cardiaque)
- Random Nerd Tutorials, [ESP32 MQTT Publish Subscribe](https://randomnerdtutorials.com/esp32-mqtt-publish-subscribe-arduino-ide/)
- N. O'Leary, [PubSubClient](https://github.com/knolleary/pubsubclient)
- [Node-RED documentation](https://nodered.org/docs/) · [node-red-dashboard](https://flows.nodered.org/node/node-red-dashboard)
- [Eclipse Mosquitto](https://mosquitto.org/)
- Espressif, [ESP32 Arduino Core : I2C](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/i2c.html)

---

## Équipe
- Oumayma Mhiri
- Amira Ben Cheikh Belgacem
- Nour Hammadi

Encadrants : A. Combes 




