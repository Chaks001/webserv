# Webserv

Une implémentation de serveur HTTP en C++98 compatible avec les exigences de l'école 42.

## Compilation

Pour compiler le projet, exécutez :

```bash
make
```

Cela générera l'exécutable `webserv`.

> **Note pour les utilisateurs Windows** : Le projet est conçu pour Linux mais inclut des couches de compatibilité pour Windows (MinGW). Utilisez `make re` si vous rencontrez des problèmes.

## Utilisation

Lancez le serveur avec un fichier de configuration :

```bash
./webserv [fichier_de_config]
```

Si aucun argument n'est fourni, il utilise par défaut `config/default.conf`.

Exemple :
```bash
./webserv config/default.conf
```

## Configuration

Le fichier de configuration utilise une syntaxe similaire à nginx. Directives principales :

- `server` : Définit un bloc serveur.
- `listen` : Port d'écoute.
- `server_name` : Nom du serveur/domaine.
- `root` : Répertoire racine pour les fichiers statiques.
- `location` : Définit les règles de routage pour des chemins URI spécifiques.
  - `allow_methods` : Méthodes HTTP autorisées (GET, POST, DELETE).
  - `autoindex` : Active/désactive le listing des répertoires (`on`/`off`).
  - `upload_store` : Répertoire pour sauvegarder les fichiers uploadés.
  - `cgi_pass` : Associe les extensions de fichiers aux interpréteurs CGI (ex: `.py /usr/bin/python3`).

## Tests

### 1. Fichiers Statiques (GET)
Ouvrez votre navigateur sur `http://localhost:8080/` ou utilisez curl :
```bash
curl -v http://localhost:8080/
```

### 2. Upload de Fichier (POST)
Pour uploader un fichier (assurez-vous que le dossier `www/uploads` existe) :
```bash
curl -v -X POST -d "Contenu du fichier" http://localhost:8080/uploaded_file.txt
```
Le fichier sera sauvegardé dans le répertoire `upload_store` configuré.

### 3. Supprimer un Fichier (DELETE)
Pour supprimer le fichier uploadé :
```bash
curl -v -X DELETE http://localhost:8080/uploaded_file.txt
```

### 4. Exécution CGI
Pour tester le CGI (nécessite Python installé) :
```bash
curl -v http://localhost:8080/test.py
```
> **Note** : Sur Windows, l'exécution CGI est simulée (stub) et renverra une erreur 500 ou un message "Not supported". Sur Linux, le script sera exécuté.

## Fonctionnalités Implémentées
- **E/S Non-bloquantes** : Utilise `poll()` (ou `WSAPoll` sur Windows) pour le multiplexage d'événements.
- **Méthodes HTTP** : GET, POST, DELETE.
- **Encodage Chunked** : Décode les corps de requêtes chunked.
- **CGI** : Support pour l'exécution de scripts dynamiques (Linux).
- **Autoindex** : Génère le listing des répertoires.
- **Service de Fichiers Statiques** : Sert HTML, CSS, JS, etc.
