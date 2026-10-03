# Chrono Broadcast — CARAC TIMER

Open-source **broadcast timer / studio clock system** designed for live production, with a browser-based control interface, a HUB75 LED display controller and a small wireless ESP8266 remote.

The project is built around a simple goal: provide a reliable on-set timer that keeps running locally even if the control computer or Wi-Fi connection is interrupted.

---

# Français

## Présentation

**Chrono Broadcast / CARAC TIMER** est un système de chrono broadcast destiné aux plateaux TV, studios, événements live et régies.

Le système est prévu pour combiner :

- un afficheur LED HUB75 64×32 ;
- un contrôleur principal ESP32-S3 ;
- une interface régie dans un navigateur ;
- une petite télécommande ESP8266 sur batterie ;
- des commandes PLAY / PAUSE / RESET ;
- plusieurs modes d'affichage : horloge, chronomètre, countdown, heure cible, standby ;
- configuration réseau sans reflasher ;
- mise à jour OTA sans ouvrir le boîtier.

## Télécommande ESP8266

La télécommande actuelle utilise un **NodeMCU ESP8266** avec un seul bouton physique :

- appui court → **PLAY / PAUSE**
- appui long 1,5 s → **RESET**

Elle expose également une interface Web locale permettant de :

- tester PLAY / PAUSE / RESET ;
- afficher la batterie en volts et en pourcentage ;
- afficher RSSI, canal Wi-Fi, BSSID, MAC, IP, gateway, masque et DNS ;
- choisir entre **DHCP** et **IP fixe** ;
- configurer IP, gateway, subnet, DNS1 et DNS2 ;
- modifier le hostname ;
- modifier le SSID et le mot de passe ;
- modifier le Wi-Fi de secours ;
- redémarrer l'ESP ;
- effectuer une mise à jour **OTA** depuis le navigateur.

## Mode secours Wi-Fi

Si le réseau enregistré n'est plus disponible, la télécommande démarre automatiquement un point d'accès :

```text
SSID : CARAC-REMOTE-SETUP
Mot de passe : caracremote
Adresse : http://192.168.4.1
```

Le portail permet de modifier les paramètres réseau sans câble USB.

## DHCP ou IP fixe

Deux modes sont disponibles.

### DHCP

Le routeur fournit automatiquement :

- adresse IP ;
- gateway ;
- masque de sous-réseau ;
- DNS.

C'est le mode recommandé pour les tests et les réseaux mobiles.

### IP statique

La télécommande permet de renseigner :

- adresse IP ;
- gateway ;
- subnet mask ;
- DNS primaire ;
- DNS secondaire.

Pour une installation broadcast fixe, gardez l'IP statique **hors de la plage DHCP** du routeur, ou utilisez une réservation DHCP.

## Paramètres réseau disponibles

L'interface affiche ou permet de configurer les paramètres suivants :

| Paramètre | Disponible |
|---|---|
| SSID | Oui |
| Mot de passe Wi-Fi | Oui |
| DHCP / IP fixe | Oui |
| Adresse IP | Oui |
| Gateway | Oui |
| Subnet mask | Oui |
| DNS primaire | Oui |
| DNS secondaire | Oui |
| Hostname | Oui |
| mDNS | Oui |
| RSSI | Oui |
| Canal Wi-Fi | Oui |
| BSSID | Oui |
| Adresse MAC | Oui |
| AP de secours | Oui |

## Veille automatique

La télécommande peut maintenant passer automatiquement en **Light Sleep** après une durée d'inactivité configurable depuis son interface Web :

- **Jamais**
- délai configurable en **minutes**
- délai configurable en **heures**

Aucune modification du câblage du bouton n'est nécessaire : le bouton existant sur **D5 / GPIO14** est utilisé comme source de réveil. Lorsqu'elle dort, un appui sur PLAY / PAUSE réveille la télécommande et ce même appui est envoyé comme commande PLAY / PAUSE.

Le point d'accès de secours reste prioritaire : la télécommande ne s'endort pas lorsqu'elle est en mode `CARAC-REMOTE-SETUP`, afin de conserver l'accès aux réglages réseau.

La veille utilisée est **Light Sleep**, et non Deep Sleep, car l'ESP8266 permet le réveil GPIO en Light Sleep sur GPIO14 sans modifier le câblage. Le Wi-Fi et l'interface Web sont indisponibles pendant la veille et reviennent après le réveil.

## Batterie

La batterie LiPo 1S est mesurée sur `A0` à travers un pont diviseur :

```text
+ batterie après interrupteur
        │
      100 kΩ
        │
        ├────► A0
        │
      100 kΩ
        │
       GND
```

Le firmware applique actuellement un coefficient de calibration de :

```text
1.114
```

calculé à partir d'une mesure de référence :

```text
Multimètre : 4,10 V
Lecture initiale ESP : 3,68 V
```

La jauge batterie reste une estimation basée sur la tension LiPo.

## Câblage télécommande

### Alimentation

```text
LiPo +  → B+ module charge/protection USB-C
LiPo -  → B- module charge/protection USB-C

OUT+ module → interrupteur → IN+ MT3608
OUT- module ──────────────→ IN- MT3608

OUT+ MT3608 réglé à 5,00 V → VIN ESP8266
OUT- MT3608                → GND ESP8266
```

### Bouton

```text
D5 / GPIO14 → bouton → GND
```

Le firmware utilise `INPUT_PULLUP`, aucune résistance externe n'est nécessaire pour le bouton.

### Batterie

```text
+ après interrupteur → 100 kΩ → A0 → 100 kΩ → GND
```

> Ne jamais relier directement le positif et le négatif du module de charge.

## Firmware

Source principale :

```text
firmware/CHRONO_BROADCAST_REMOTE_ESP8266.ino
```

Carte Arduino IDE :

```text
NodeMCU 1.0 (ESP-12E Module)
```

FQBN :

```text
esp8266:esp8266:nodemcuv2
```

## Première installation

La première installation doit être effectuée par USB.

1. Installer Arduino IDE.
2. Installer **esp8266 by ESP8266 Community**.
3. Sélectionner **NodeMCU 1.0 (ESP-12E Module)**.
4. Ouvrir le firmware.
5. Compiler et téléverser.
6. Au premier démarrage sans configuration Wi-Fi valide, se connecter à `CARAC-REMOTE-SETUP`.
7. Ouvrir `http://192.168.4.1`.
8. Enregistrer le réseau.

Les identifiants Wi-Fi personnels ne sont pas stockés dans le dépôt GitHub.

## Mise à jour OTA

GitHub Actions compile automatiquement le firmware.

La dernière Release fournit notamment :

```text
CHRONO_BROADCAST_REMOTE_OTA.bin
```

Depuis la télécommande :

1. ouvrir son interface Web ;
2. cliquer sur **OUVRIR LA MISE À JOUR OTA** ;
3. sélectionner `CHRONO_BROADCAST_REMOTE_OTA.bin` ;
4. attendre le redémarrage.

Ne coupez pas l'alimentation pendant le flash.

Le firmware supporte aussi **ArduinoOTA** depuis Arduino IDE lorsqu'un PC et la télécommande sont sur le même réseau.

## API

### État

```http
GET /status
```

Exemple :

```json
{
  "running": true,
  "last_action": "PLAY",
  "counter": 12,
  "wifi_connected": true,
  "ap_mode": false,
  "network_mode": "DHCP",
  "hostname": "carac-remote",
  "rssi": -48,
  "channel": 6,
  "ip": "192.168.1.42",
  "gateway": "192.168.1.1",
  "subnet": "255.255.255.0",
  "dns1": "192.168.1.1",
  "battery_voltage": 4.10,
  "battery_percent": 90,
  "firmware": "1.8.0"
}
```

## Structure du dépôt

```text
/
├── README.md
├── LICENSE
├── CHANGELOG.md
├── firmware/
│   └── CHRONO_BROADCAST_REMOTE_ESP8266.ino
└── .github/
    └── workflows/
        ├── build-firmware.yml
        └── release.yml
```

## État du projet

Le dépôt est en développement actif. La partie télécommande ESP8266 est actuellement fonctionnelle. Le contrôleur principal ESP32-S3 et l'intégration finale de l'afficheur HUB75 seront ajoutés progressivement.

## Contributions

Les Issues et Pull Requests sont bienvenues.

Auteur : [CharlesGasta](https://github.com/CharlesGasta)

---

# English

## Overview

**Chrono Broadcast / CARAC TIMER** is an open-source broadcast timer system for studios, live productions and events.

The project is designed around:

- a 64×32 HUB75 LED display;
- an ESP32-S3 master controller;
- a browser-based control interface;
- a battery-powered ESP8266 remote;
- PLAY / PAUSE / RESET commands;
- clock, stopwatch, countdown, target-time and standby modes;
- configurable DHCP or static networking;
- automatic rescue Wi-Fi portal;
- browser-based OTA firmware updates.

The ESP8266 remote is fully configurable from its own Web interface and does not require USB access after the initial firmware installation.

## Remote control

- short press → PLAY / PAUSE
- 1.5 s long press → RESET
- LiPo battery monitoring
- DHCP / static IPv4
- configurable gateway, subnet and DNS
- hostname + mDNS
- automatic rescue AP
- browser OTA
- ArduinoOTA
- JSON status API

## License

Released under the **MIT License**. See [LICENSE](LICENSE).

## Disclaimer

This project is provided as-is without warranty. Verify polarity, voltages and wiring before applying power. LiPo batteries can be hazardous if shorted, overcharged, punctured or incorrectly wired.
