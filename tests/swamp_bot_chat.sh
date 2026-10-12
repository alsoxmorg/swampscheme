#!/bin/sh

bot_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

exec "$bot_dir/swampscheme" "$bot_dir/scripts/swamp_bot_chat.scm"
