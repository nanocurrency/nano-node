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

pub fn launch_node(
    nano_node: &Path,
    tmp_dir: &Path,
    handle: Handle,
    i: u64,
) -> Result<(Child, RpcClient)> {
    let data_dir = tmp_dir.join(format!("Nano_load_test_{}", i));
    match fs::create_dir(&data_dir) {
        Ok(_) => {}
        Err(ref e) if e.kind() == io::ErrorKind::AlreadyExists => {
            let _ = fs::remove_file(data_dir.join("data.ldb"));
        }
        r => r.chain_err(|| "failed to create nano_node data directory")?,
    }
    let rpc_port = RPC_PORT_START + i;
    let peering_port = PEERING_PORT_START + i;
    let config = json!({
        "version": "2",
        "rpc_enable": "true",
        "rpc": {
            "address": "::1",
            "port": rpc_port.to_string(),
            "enable_control": "true",
            "frontier_request_limit": "16384",
            "chain_request_limit": "16384",
        },
        "node": {
            "version": "8",
            "peering_port": peering_port.to_string(),
            "bootstrap_fraction_numerator": "1",
            "receive_minimum": "1000000000000000000000000",
            "logging": {
                "version": "2",
                "ledger": "false",
                "ledger_duplicate": "false",
                "vote": "false",
                "network": "true",
                "network_message": "false",
                "network_publish": "false",
                "network_packet": "false",
                "network_keepalive": "false",
                "node_lifetime_tracing": "false",
                "insufficient_work": "true",
                "log_rpc": "true",
                "bulk_pull": "false",
                "work_generation_time": "true",
                "log_to_cerr": "false",
                "max_size": "16777216",
            },
            "work_peers": "",
            "preconfigured_peers": "",
            "preconfigured_representatives": [
                "xrb_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo"
            ],
            "inactive_supply": "0",
            "password_fanout": "1024",
            "io_threads": "8",
            "work_threads": "8",
            "enable_voting": "true",
            "bootstrap_connections": "4",
            "callback_address": "",
            "callback_port": "0",
            "callback_target": "",
            "lmdb_max_dbs": "128",
        },
        "opencl_enable": "false",
        "opencl": {
            "platform": "0",
            "device": "0",
            "threads": "1048576",
        }
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
    let rpc_client = RpcClient::new(
        handle,
        format!("http://[::1]:{}/", rpc_port).parse().unwrap(),
    );
    Ok((child, rpc_client))
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
    })).then(|x| x.chain_err(|| "failed to call nano_node RPC"))
            .map(|_| ()),
    ) as _
}
