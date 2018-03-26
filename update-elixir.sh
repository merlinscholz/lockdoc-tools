#!/bin/bash
LXR_DIR=/fs/data/www/lockdebug/elixir
export LXR_REPO_DIR=/fs/data/www/lockdebug/projects/Linux/repo/
export LXR_DATA_DIR=/fs/data/www/lockdebug/projects/Linux/data/

DIR=`pwd`

cd ${LXR_REPO_DIR}
git pull
cd ${DIR}

cd ${LXR_DIR}
./update.py
cd ${DIR}
