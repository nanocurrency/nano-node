use std::io;
use std::fs;
use std::fs::File;
use std::path::Path;
use std::process::Command;

use serde_json;

use futures::Future;
use hyper::client::Connect;

use tokio_core::reactor::Handle;
use tokio_process::{Child, CommandExt};

use serde_json::Value;

use errors::*;

use rpc::RpcClient;

const RPC_PORT_START: u64 = 55000;
const PEERING_PORT_START: u64 = 54000;
const IPC_PORT_START: u64 = 56000;

pub fn launch_node_and_rpc(
    nano_node: &Path,
    nano_rpc: &Path,
    tmp_dir: &Path,
    handle: Handle,
    i: u64,
) -> Result<(Child, Child, RpcClient)> {
    let data_dir = tmp_dir.join(format!("Nano_load_test_{}", i));
    match fs::create_dir(&data_dir) {
        Ok(_) => {}
        Err(ref e) if e.kind() == io::ErrorKind::AlreadyExists => {
            let _ = fs::remove_file(data_dir.join("data.ldb"));
            let _ = fs::remove_file(data_dir.join("wallets.ldb"));
        }
        r => r.chain_err(|| "failed to create nano_node data directory")?,
    }
    let peering_port = PEERING_PORT_START + i;
    let ipc_port = IPC_PORT_START + i;

    let config = json!({
    "version": "3",
    "rpc_enable": "false",
    "node": {
        "version": "17",
        "peering_port": peering_port.to_string(),
        "bootstrap_fraction_numerator": "1",
        "receive_minimum": "1000000000000000000000000",
        "logging": {
            "version": "7",
            "ledger": "false",
            "ledger_duplicate": "false",
            "vote": "false",
            "network": "true",
            "network_message": "false",
            "network_publish": "false",
            "network_packet": "false",
            "network_keepalive": "false",
            "network_node_id_handshake": "false",
            "node_lifetime_tracing": "false",
            "insufficient_work": "true",
            "log_ipc": "false",
            "bulk_pull": "false",
            "work_generation_time": "true",
            "upnp_details": "false",
            "timing": "false",
            "log_to_cerr": "false",
            "max_size": "134217728",
            "rotation_size": "4194304",
            "flush": "true",
            "min_time_between_output": "5"
        },
        "work_peers": "",
        "preconfigured_peers": "",
        "preconfigured_representatives": [
            "xrb_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo"
        ],
        "online_weight_minimum": "60000000000000000000000000000000000000",
        "online_weight_quorum": "50",
        "password_fanout": "1024",
        "io_threads": "8",
        "network_threads": "8",
        "work_threads": "8",
        "signature_checker_threads": "7",
        "enable_voting": "true",
        "bootstrap_connections": "4",
        "bootstrap_connections_max": "64",
        "callback_address": "",
        "callback_port": "0",
        "callback_target": "",
        "lmdb_max_dbs": "128",
        "block_processor_batch_max_time": "5000",
        "allow_local_peers": "true",
        "vote_minimum": "1000000000000000000000000000000000",
        "unchecked_cutoff_time": "14400",
        "ipc": {
            "tcp": {
                "enable": "true",
                "port": ipc_port.to_string (),
                "io_timeout": "15"
            },
            "local": {
                "enable": "false",
                "path": "/tmp/nano",
                "io_timeout": "15"
            },
            "version": "1",
            "enable_sign_hash": "false"
        },
        "tcp_client_timeout": "5",
        "tcp_server_timeout": "30"
    },
    "opencl_enable": "false",
    "opencl": {
        "platform": "0",
        "device": "0",
        "threads": "1048576"
        }
    });

    let rpc_port = RPC_PORT_START + i;
    let rpc_config = json!({
        "address": "::1",
        "port": rpc_port.to_string(),
        "enable_control": "true",
        "max_json_depth": "20",
        "version": "1",
        "ipc_port": ipc_port.to_string (),
        "io_threads": "8",
        "num_ipc_connections" : "8"
    });

    let config_writer =
        File::create(data_dir.join("config.json")).chain_err(|| "failed to create config.json")?;
    serde_json::to_writer_pretty(config_writer, &config)
        .chain_err(|| "failed to write config.json")?;
    let child = Command::new(nano_node)
        .arg("--data_path")
        .arg(&data_dir)
        .arg("--daemon")
        .spawn_async(&handle)
        .chain_err(|| "failed to spawn nano_node")?;

    let rpc_config_writer =
        File::create(data_dir.join("rpc_config.json")).chain_err(|| "failed to create rpc_config.json")?;
    serde_json::to_writer_pretty(rpc_config_writer, &rpc_config)
        .chain_err(|| "failed to write rpc_config.json")?;
    let rpc_child = Command::new(nano_rpc)
        .arg("--data_path")
        .arg(&data_dir)
        .arg("--daemon")
        .spawn_async(&handle)
        .chain_err(|| "failed to spawn nano_rpc")?;

    let rpc_client = RpcClient::new(
        handle,
        format!("http://[::1]:{}/", rpc_port).parse().unwrap(),
    );
    Ok((child, rpc_child, rpc_client))
}

pub fn connect_node<C: Connect>(
    node: &RpcClient<C>,
    i: u64,
) -> Box<Future<Item = (), Error = Error>> {
    Box::new(
        node.call::<_, Value>(&json!({
        "action": "keepalive",
        "address": "::1",
        "port": PEERING_PORT_START + i,
    })).then(|x| x.chain_err(|| "failed to call nano_rpc"))
            .map(|_| ()),
    ) as _
}
