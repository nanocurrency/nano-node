#!/usr/bin/env bash
set -Eeuo pipefail

readonly CONFIG_SRC_DIR="/usr/share/nano/config"

usage() {
	cat <<'EOF'
Usage:
  [daemon] [--vacuum-over <size>] [cli_options]
    daemon
      start as daemon (default mode)

    cli_options
      nano_node CLI options (see nano_node --help)

    --vacuum-over <size>
      vacuum database if over size GB on startup (daemon mode only)
      (deprecated alias: -v<size> or -v <size>)

  sh [command]
    command
      shell passthrough (defaults to $SHELL)

Note: 'nano_node' is the default executable and can be omitted.
Use --docker-help to show this message.
EOF
}

log() { printf '%s\n' "$*"; }
err() { printf 'ERROR: %s\n' "$*" >&2; }
die() {
	err "$@"
	exit 1
}

read_network() {
	# live | beta | dev | test ; default live
	local n=''
	if [[ -r /etc/nano-network ]]; then
		read -r n </etc/nano-network || n=''
	fi
	case "$n" in
	live | '') echo live ;;
	beta | dev | test) echo "$n" ;;
	*) echo live ;;
	esac
}

network_suffix() {
	case "$1" in
	live) echo '' ;;
	beta) echo 'Beta' ;;
	dev) echo 'Dev' ;;
	test) echo 'Test' ;;
	*) echo '' ;;
	esac
}

ensure_data_directory() {
	# Move legacy dir if needed, then ensure target exists
	local legacy_dir="$1" target_dir="$2"
	if [[ -d "$legacy_dir" && ! -d "$target_dir" ]]; then
		log "Renaming legacy directory $legacy_dir to $target_dir"
		mv -- "$legacy_dir" "$target_dir"
	fi
	mkdir -p -- "$target_dir"
}

ensure_configs() {
	local target_dir="$1"
	if [[ ! -f "$target_dir/config-node.toml" && ! -f "$target_dir/config.json" ]]; then
		log "Config file not found, copying defaults"
		cp -- "$CONFIG_SRC_DIR/config-node.toml" "$target_dir/config-node.toml"
		cp -- "$CONFIG_SRC_DIR/config-rpc.toml" "$target_dir/config-rpc.toml"
	fi
}

parse_vacuum_size() {
	local v="${1:-}"
	[[ "$v" =~ ^[0-9]+$ ]] || die "Invalid vacuum size '$v'. Expected integer GB."
	echo "$v"
}

maybe_vacuum() {
	# Only in daemon mode, when limit set, and DB file exceeds limit
	local limit_gb="$1" db_file="$2"
	((DAEMON_MODE && limit_gb > 0)) || return 0
	[[ -f "$db_file" ]] || return 0

	local bytes
	if ! bytes="$(stat --format=%s "$db_file" 2>/dev/null)"; then
		log "Unable to determine database size for $db_file"
		return 0
	fi
	local limit_bytes=$((limit_gb * 1024 * 1024 * 1024))
	if ((bytes > limit_bytes)); then
		local size_gb=$((bytes / 1024 / 1024 / 1024))
		log "Current ledger size: ${size_gb}GB (${bytes} bytes) is over threshold: ${limit_gb}GB. Vacuuming..."
		nano_node --vacuum
		log "Database vacuum completed"
	fi
}

# Parsed args / state
DAEMON_MODE=0
VACUUM_THRESHOLD_GB=0
EXEC="nano_node" # default executable is nano_node
declare -a PASSTHROUGH=()
declare -a CMD=() # filled after we finalize EXEC

parse_args() {
	# "sh ..." -> shell passthrough
	if [[ ${1:-} == 'sh' ]]; then
		shift || true
		if (($# == 0)); then
			log "SHELL PASSTHROUGH: ${SHELL:-/bin/bash}"
			exec "${SHELL:-/bin/bash}"
		else
			log "COMMAND PASSTHROUGH: $*"
			exec "$@"
		fi
	fi

	# First argument may be an explicit executable: nano_node or nano_rpc
	case "${1:-}" in
	nano_node)
		EXEC="nano_node"
		shift || true
		;;
	nano_rpc)
		EXEC="nano_rpc"
		shift || true
		;;
	esac

	CMD=("$EXEC")

	while (($# > 0)); do
		case "$1" in
		daemon)
			DAEMON_MODE=1
			CMD+=('--daemon')
			;;
		--vacuum-over)
			shift || die "Option --vacuum-over requires a size in GB"
			VACUUM_THRESHOLD_GB="$(parse_vacuum_size "${1:-}")"
			log "Vacuum DB if over ${VACUUM_THRESHOLD_GB} GB on startup"
			;;
		--vacuum-over=*)
			VACUUM_THRESHOLD_GB="$(parse_vacuum_size "${1#--vacuum-over=}")"
			log "Vacuum DB if over ${VACUUM_THRESHOLD_GB} GB on startup"
			;;
		-l)
			# Deprecated, retained for backwards compatibility
			;;
		--docker-help)
			usage
			exit 0
			;;
		*)
			PASSTHROUGH+=("$1")
			;;
		esac
		shift || true
	done

	if ((${#PASSTHROUGH[@]})); then
		CMD+=("${PASSTHROUGH[@]}")
	fi
}

parse_args "$@"

NETWORK="$(read_network)"
SUFFIX="$(network_suffix "$NETWORK")"
LEGACY_DIR="${HOME}/RaiBlocks${SUFFIX}"
DATA_DIR="${HOME}/Nano${SUFFIX}"
DB_FILE="${DATA_DIR}/data.ldb"

ensure_data_directory "$LEGACY_DIR" "$DATA_DIR"
ensure_configs "$DATA_DIR"
maybe_vacuum "$VACUUM_THRESHOLD_GB" "$DB_FILE"

log "EXECUTING: ${CMD[*]}"
exec "${CMD[@]}"
