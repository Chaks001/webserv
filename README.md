# Webserv

Une implémentation de serveur HTTP en C++98 compatible avec les exigences de l'école 42.

## Compilation

Pour compiler le projet, exécutez :

```bash
make
```

Cela générera l'exécutable `webserv`.


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

Les exemples ci-dessous supposent le serveur lancé avec `config/eval.conf` :
```bash
./webserv config/eval.conf
```

### 1. Fichiers Statiques (GET)
Ouvrez votre navigateur sur `http://localhost:8080/` ou utilisez curl :
```bash
curl -v http://localhost:8080/
```

### 2. Upload de Fichier (POST)
Depuis un formulaire HTML ou avec `-F`, le fichier est enregistré sous son nom d'origine :
```bash
curl -v -F "file=@mon_fichier.txt" http://localhost:8080/uploads/
```
Avec un corps brut, le nom vient de l'URL :
```bash
curl -v -X POST --data-binary "Contenu du fichier" http://localhost:8080/uploads/mon_fichier.txt
```
Le fichier est enregistré dans le répertoire `upload_store` configuré, puis récupérable :
```bash
curl -v http://localhost:8080/uploads/mon_fichier.txt
```

### 3. Supprimer un Fichier (DELETE)
```bash
curl -v -X DELETE http://localhost:8080/uploads/mon_fichier.txt
```

### 4. Exécution CGI
Pour tester le CGI (nécessite Python installé) :
```bash
curl -v http://localhost:8080/test.py
```

## Fonctionnalités Implémentées
- **E/S Non-bloquantes** : Utilise `poll()` pour le multiplexage d'événements.
- **Méthodes HTTP** : GET, POST, DELETE.
- **Encodage Chunked** : Décode les corps de requêtes chunked.
- **CGI** : Support pour l'exécution de scripts dynamiques (Linux).
- **Autoindex** : Génère le listing des répertoires.
- **Service de Fichiers Statiques** : Sert HTML, CSS, JS, etc.
