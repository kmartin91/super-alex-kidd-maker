# Publier une version (à la main, sur GitHub)

L'app se met à jour toute seule : au démarrage, elle lit `latest.json` sur la
dernière release GitHub et propose d'installer la nouvelle version. Les paquets
de mise à jour sont signés avec la clé `~/.tauri/super-alex-kidd-maker.key`.

**Garde cette clé, et une copie ailleurs (clé USB, gestionnaire de mots de
passe).** Sans elle, les apps déjà installées ne pourront plus être mises à
jour. Sa clé publique est dans `app/src-tauri/tauri.conf.json`
(`plugins.updater.pubkey`).

## 1. Le numéro de version

Augmente `version` dans `app/src-tauri/tauri.conf.json` (par exemple `0.2.0`).
L'app ne propose une mise à jour que si la version est plus récente.

## 2. Construire

| Plateforme | Commande | Où |
|---|---|---|
| Mac (Intel + Apple Silicon) | `npm run release:mac` (avec tes variables `APPLE_*` pour signer et notariser) | `app/src-tauri/target/universal-apple-darwin/release/bundle/` |
| Linux (x86_64 et ARM) | `npm run release:linux` (Docker) | `app/src-tauri/target/linux/amd64/` et `arm64/` |
| Windows | `npm run release:win` sur le PC Windows, avec la clé copiée dans `%USERPROFILE%\.tauri\` | `app/src-tauri/target/release/bundle/nsis/` |

Chaque paquet de mise à jour a son fichier `.sig` à côté.

## 3. latest.json

    npm run release:json -- "Ce qui change dans cette version"

écrit `app/src-tauri/target/latest.json` avec les signatures et les adresses de
téléchargement (`https://github.com/kmartin91/super-alex-kidd-maker/releases/download/v<version>/…`).
Construis toutes les plateformes voulues avant (sur le même dossier `target`,
ou recopie les fichiers de Windows dedans).

## 4. La release GitHub

Crée la release avec le tag **`v<version>`** (exactement, par exemple `v0.2.0`),
et joins :

- les installateurs : `.dmg`, `.deb`, `.AppImage`, `-setup.exe` ;
- les paquets de mise à jour : `Super Alex Kidd Maker.app.tar.gz` (Mac), les
  `.AppImage` (Linux), `-setup.exe` (Windows) ;
- `latest.json`.

GitHub remplace les espaces des noms de fichiers par des points : c'est prévu
dans `latest.json`.

## Signer l'installateur Windows (facultatif)

Sans signature, Windows affiche « Windows a protégé votre ordinateur » au premier lancement
(« Informations complémentaires », puis « Exécuter quand même »). Pour l'éviter, il faut un
certificat de signature de code :

- **Certum « Open Source Code Signing »** : le moins cher pour un projet open source (quelques
  dizaines d'euros par an), utilisable à la main avec `signtool` sur ton PC ;
- **Azure Trusted Signing** : environ 10 $ par mois, signe aussi en local ;
- **SignPath Foundation** : gratuit pour l'open source, mais la signature passe par une
  construction automatique (GitHub Actions), que tu ne veux pas.

La réputation SmartScreen vient ensuite avec le nombre de téléchargements.
