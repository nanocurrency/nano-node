#![recursion_limit = "128"]

use std::process;
use std::path::PathBuf;
use std::thread;
use std::time::{Duration, Instant};
use std::collections::HashMap;
use std::iter;
use std::env;

extern crate clap;
use clap::Arg;

#[macro_use]
extern crate error_chain;

extern crate futures;
use futures::{stream, Future, Stream};

extern crate serde;
#[macro_use]
extern crate serde_derive;
#[macro_use]
extern crate serde_json;
use serde_json::Value;

extern crate tokio_core;
use tokio_core::reactor::Core;

extern crate tokio_process;

extern crate hyper;

extern crate rand;
use rand::Rng;

extern crate time;

extern crate env_logger;

mod errors {
    error_chain!{}
}
use errors::*;

mod rpc;
use rpc::{RpcClient, RpcError};

mod launch_node;

struct Parameters {
    node_count: u16,
    node_path: PathBuf,
    tmp_dir: PathBuf,
    send_count: usize,
    dest_count: usize,
    simultaneous_process_calls: usize,
    catch_up_timeout: u64,
    generate_receives: bool,
    precompute_blocks: bool,
    output_stats: bool,
}

// found in secure.cpp
const GENESIS_ACCOUNT: &str = "xrb_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo";
const GENESIS_PRIVKEY: &str = "34F0A37AAD20F4A260F0A5B3CB3D7FB50673212263E58A380BC10474BB039CE4";

fn run(params: Parameters) -> Result<()> {
    let output_stats = params.output_stats;
    macro_rules! tstat {
        ($( $x:tt )*) => {
            if output_stats {
                let now = time::now().to_timespec();
                println!("{}.{:09},{}", now.sec, now.nsec, format_args!( $( $x )* ));
            }
        }
    }
    let mut tokio_core = Core::new().chain_err(|| "failed to create tokio Core")?;
    let mut children = Vec::with_capacity(params.node_count as _);
    let mut nodes: Vec<RpcClient<_>> = Vec::with_capacity(params.node_count as _);
    for i in 0..params.node_count {
        let (child, rpc_client) = launch_node::launch_node(
            &params.node_path,
            &params.tmp_dir,
            tokio_core.handle(),
            i as _,
        )?;
        children.push(child);
        nodes.push(rpc_client);
    }
    if nodes.is_empty() {
        bail!("no nodes spun up");
    }
    eprintln!("Waiting for nodes to spin up...");
    thread::sleep(Duration::from_secs(7));
    eprintln!("Connecting nodes...");
    let primary_node = nodes.first().unwrap();
    for (a, node) in nodes.iter().enumerate() {
        for b in 0..nodes.len() {
            if a != b {
                tokio_core.run(launch_node::connect_node(node, b as _))?;
            }
        }
    }
    thread::sleep(Duration::from_secs(5));
    eprintln!("Beginning tests");
    tstat!("start");
    #[derive(Debug, Deserialize, PartialEq, Eq)]
    struct AccountInfo {
        frontier: String,
        balance: String,
        block_count: String,
    }
    tstat!("genesis_info,start");
    let genesis_initial = primary_node.call(&json!({
        "action": "account_info",
        "account": GENESIS_ACCOUNT,
    }));
    let genesis_initial: AccountInfo = tokio_core
        .run(genesis_initial)
        .chain_err(|| "failed to get genesis account info")?;
    tstat!("genesis_info,done");
    let genesis_initial_balance = genesis_initial.balance;
    #[derive(Deserialize)]
    #[allow(dead_code)]
    struct Account {
        account: String,
        private: String,
        public: String,
    }
    tstat!("key_create,start");
    let dest_accts = stream::iter_ok(0..params.dest_count)
        .map(|_| {
            primary_node.call(&json!({
                "action": "key_create",
            }))
        })
        .buffer_unordered(10) // execute 10 `key_create`s simultaniously
        .inspect(|_| {
            tstat!("key_create,progress");
        })
        .collect();
    tstat!("key_create,done");
    let dest_accts: Vec<Account> = tokio_core
        .run(dest_accts)
        .chain_err(|| "failed to generate destination accounts")?;
    if dest_accts.is_empty() {
        bail!("no destination accounts generated");
    }
    let mut frontiers = HashMap::new();
    #[derive(Deserialize)]
    struct BlockInfo {
        hash: String,
        block: String,
    }
    let mut rng: rand::XorShiftRng = rand::thread_rng().gen();
    if params.precompute_blocks {
        frontiers.insert(GENESIS_ACCOUNT, genesis_initial.frontier);
        let mut last_percent = 0;
        eprint!("Creating blocks: 00%");
        tstat!("block_create,start");
        let mut blocks: Vec<String> = Vec::new();
        for i in 0..params.send_count {
            let dest_acct = &rng.choose(&dest_accts).unwrap();
            // since only `balance - amount` matters, each block spends 1 raw
            let send_future = primary_node.call(&json!({
                    "action": "block_create",
                    "type": "send",
                    "key": GENESIS_PRIVKEY,
                    "balance": genesis_initial_balance,
                    "amount": i + 1,
                    "destination": dest_acct.account,
                    "previous": frontiers[GENESIS_ACCOUNT],
                }));
            // needs to be synchronous because of frontier ordering
            let send: BlockInfo = tokio_core
                .run(send_future)
                .chain_err(|| "failed to create send block")?;
            tstat!("block_create,progress,send");
            blocks.push(send.block);
            frontiers.insert(GENESIS_ACCOUNT, send.hash.clone());
            if params.generate_receives {
                let recv_future = if let Some(frontier) = frontiers.get(dest_acct.account.as_str())
                {
                    primary_node.call(&json!({
                            "action": "block_create",
                            "type": "receive",
                            "key": dest_acct.private,
                            "previous": frontier,
                            "source": send.hash,
                        }))
                } else {
                    primary_node.call(&json!({
                            "action": "block_create",
                            "type": "open",
                            "key": dest_acct.private,
                            "source": send.hash,
                            "representative": GENESIS_ACCOUNT,
                        }))
                };
                let recv: BlockInfo = tokio_core
                    .run(recv_future)
                    .chain_err(|| "failed to create receive block")?;
                tstat!("block_create,progress,receive");
                frontiers.insert(&dest_acct.account, recv.hash);
                blocks.push(recv.block);
            }
            let new_percent = (100 * i) / params.send_count;
            if last_percent == new_percent {
                continue;
            }
            last_percent = new_percent;
            eprint!("\rCreating blocks: {:02}%", new_percent);
        }
        eprintln!("\rCreated blocks            ");
        tstat!("block_create,done");
        let mut process_calls_completed = 0;
        last_percent = 0;
        let n_blocks = blocks.len();
        eprint!("Primary node processing blocks: 00%");
        tstat!("process,start");
        let process = stream::iter_ok(blocks.iter())
            .map(|block| {
                primary_node.call::<_, Value>(&json!({
                    "action": "process",
                    "block": block,
                }))
            })
            .buffer_unordered(params.simultaneous_process_calls)
            .then(|r| match r {
                Ok(_) => Ok(()),
                Err(RpcError::RpcError(Value::String(ref s)))
                    if params.simultaneous_process_calls != 1 && s.starts_with("Gap") =>
                {
                    Ok(())
                }
                Err(err) => Err(err),
            })
            .inspect(|_| {
                process_calls_completed += 1;
                let new_percent = (100 * process_calls_completed) / n_blocks;
                if last_percent == new_percent {
                    return;
                }
                last_percent = new_percent;
                eprint!("\rPrimary node processing blocks: {:02}%", new_percent);
                tstat!("process,progress");
            });
        tokio_core
            .run(process.fold((), |_, _| Ok(())))
            .chain_err(|| "failed to process blocks")?;
        tstat!("process,done");
        eprintln!("\rPrimary node processed blocks                ");
    } else {
        #[derive(Deserialize)]
        struct WalletInfo {
            wallet: String,
        }
        let wallet = primary_node.call(&json!({
            "action": "wallet_create",
        }));
        let wallet: WalletInfo = tokio_core
            .run(wallet)
            .chain_err(|| "failed to create wallet")?;
        let wallet = wallet.wallet;
        tstat!("wallet_add_key,start");
        let add_genesis = primary_node.call::<_, Value>(&json!({
            "action": "wallet_add",
            "wallet": wallet,
            "key": GENESIS_PRIVKEY,
        }));
        tokio_core
            .run(add_genesis)
            .chain_err(|| "failed to add genesis key to wallet")?;
        tstat!("wallet_add_key,progress");
        let add_keys = stream::iter_ok(dest_accts.iter())
            .map(|acct| {
                primary_node.call::<_, Value>(&json!({
                    "action": "wallet_add",
                    "wallet": wallet,
                    "key": acct.private,
                    "work": false, // We always manually call `receive`
                }))
            })
            .buffer_unordered(10)
            .inspect(|_| tstat!("wallet_add_key,progress"));
        tokio_core
            .run(add_keys.fold((), |_, _| Ok(())))
            .chain_err(|| "failed to add keys to wallet")?;
        tstat!("wallet_add_key,done");
        let mut send_calls_completed = 0;
        let mut last_percent = 0;
        #[derive(Deserialize)]
        struct TransactionInfo {
            block: String,
        }
        eprint!("\rPrimary node processing transactions: 00%");
        tstat!("transaction,start");
        let wallet = wallet.as_str();
        let dest_accts = dest_accts.as_slice();
        let params = &params;
        let stream = stream::iter_ok(0..params.send_count)
            .map(move |_| {
                let dest_acct = rng.choose(dest_accts).unwrap();
                let send_future = primary_node
                    .call::<_, TransactionInfo>(&json!({
                    "action": "send",
                    "wallet": wallet,
                    "source": GENESIS_ACCOUNT,
                    "destination": dest_acct.account,
                    "amount": "1",
                }))
                    .inspect(move |_| {
                        tstat!("transaction,progress,send");
                    });
                if params.generate_receives {
                    Box::new(send_future.and_then(move |send| {
                        primary_node
                            .call::<_, TransactionInfo>(&json!({
                            "action": "receive",
                            "wallet": wallet,
                            "account": dest_acct.account,
                            "block": send.block,
                        }))
                            .inspect(move |_| {
                                tstat!("transaction,progress,receive");
                            })
                    })) as Box<Future<Item = TransactionInfo, Error = RpcError>>
                } else {
                    Box::new(send_future) as Box<Future<Item = TransactionInfo, Error = RpcError>>
                }
            })
            .buffer_unordered(params.simultaneous_process_calls)
            .inspect(|_| {
                send_calls_completed += 1;
                let new_percent = (100 * send_calls_completed) / params.send_count;
                if last_percent == new_percent {
                    return;
                }
                last_percent = new_percent;
                eprint!(
                    "\rPrimary node processing transactions: {:02}%",
                    new_percent
                );
            });
        tokio_core
            .run(stream.fold((), |_, _| Ok(())))
            .chain_err(|| "failed to process transactions")?;
        eprintln!("\rPrimary node processed transactions                ");
        tstat!("transaction,progress,done");
    }
    let broadcasted_at = Instant::now();
    eprintln!("Waiting for nodes to catch up...");
    let timeout = Duration::from_secs(params.catch_up_timeout);
    let mut known_account_info = HashMap::new();
    tstat!("check,start");
    for node in &nodes {
        for (&acct, frontier) in frontiers.iter() {
            // We only know frontiers if `precompute_blocks` is enabled.
            loop {
                if Instant::now() - broadcasted_at > timeout {
                    bail!("timed out while waiting for nodes to catch up");
                }
                let acct_info = node.call::<_, AccountInfo>(&json!({
                    "action": "account_info",
                    "account": acct,
                }));
                let acct_info = tokio_core
                    .run(acct_info)
                    .chain_err(|| "failed to check genesis account info")?;
                if &acct_info.frontier == frontier {
                    break;
                }
                thread::sleep(Duration::from_secs(1));
            }
        }
        if !frontiers.is_empty() {
            tstat!("check,progress");
        }
        if node as *const _ == primary_node as *const _ {
            for acct in dest_accts
                .iter()
                .map(|a| a.account.as_str())
                .chain(iter::once(GENESIS_ACCOUNT))
            {
                let acct_info = node.call(&json!({
                    "action": "account_info",
                    "account": acct,
                }));
                let acct_info: AccountInfo = tokio_core.run(acct_info).chain_err(|| {
                    format!("failed to check account {} info on primary node", acct)
                })?;
                known_account_info.insert(acct, acct_info);
            }
        } else {
            for (&acct, acct_info) in known_account_info.iter() {
                loop {
                    if Instant::now() - broadcasted_at > timeout {
                        bail!("timed out while waiting for nodes to catch up");
                    }
                    let node_acct_info = node.call(&json!({
                        "action": "account_info",
                        "account": acct,
                    }));
                    let node_acct_info = tokio_core.run(node_acct_info);
                    match node_acct_info {
                        Err(RpcError::RpcError(ref s)) if s == "Account not found" => {}
                        Ok(node_acct_info) => if acct_info == &node_acct_info {
                            break;
                        },
                        r => {
                            r.chain_err(|| {
                                format!("failed to check account {} info on secondary node", acct)
                            })?;
                        }
                    }
                    thread::sleep(Duration::from_secs(1));
                }
            }
        }
        tstat!("check,progress");
    }
    tstat!("check,done");
    eprintln!("Done!");
    tstat!("done");
    Ok(())
}

fn main() {
    env_logger::init();
    let matches = clap::App::new("nano-load-tester")
        .version(env!("CARGO_PKG_VERSION"))
        .arg(
            Arg::with_name("node_count")
                .short("n")
                .long("node_count")
                .value_name("N")
                .default_value("10")
                .help("The number of nodes to spin up"),
        )
        .arg(
            Arg::with_name("tmp_dir")
                .long("tmp-dir")
                .value_name("PATH")
                .help("The path to a temporary directory for nano_node data"),
        )
        .arg(
            Arg::with_name("node_path")
                .value_name("PATH")
                .required(true)
                .help("The path to the nano_node to test"),
        )
        .arg(
            Arg::with_name("send_count")
                .short("s")
                .short("send-count")
                .value_name("N")
                .default_value("2000")
                .help("How many send blocks to generate"),
        )
        .arg(
            Arg::with_name("destination_count")
                .short("destination-count")
                .value_name("N")
                .default_value("2")
                .help("How many destination accounts to choose between"),
        )
        .arg(
            Arg::with_name("simultaneous_process_calls")
                .long("simultaneous-process-calls")
                .value_name("N")
                .default_value("20")
                .help("How many `process` or `send` calls to send at a given time"),
        )
        .arg(
            Arg::with_name("catch_up_timeout")
                .long("catch-up-timeout")
                .value_name("SECONDS")
                .default_value("120")
                .help("The maximum number of seconds to wait for nodes to catch up"),
        )
        .arg(
            Arg::with_name("no_receives")
                .long("no-receives")
                .help("Do not generate receives for the generated sends"),
        )
        .arg(
            Arg::with_name("precompute_blocks")
                .long("precompute-blocks")
                .help("Use the `block_create` and `process` endpoints to precompute blocks"),
        )
        .arg(
            Arg::with_name("stats")
                .long("stats")
                .help("Output stats on how long steps take in CSV format"),
        )
        .get_matches();
    macro_rules! num_arg {
        ($arg:expr) => {
            match matches.value_of($arg).unwrap().parse() {
                Ok(n) => n,
                Err(err) => {
                    eprintln!("Failed to parse {}: {}", $arg, err);
                    process::exit(2);
                }
            }
        };
    }
    let params = Parameters {
        node_count: num_arg!("node_count"),
        node_path: matches.value_of("node_path").unwrap().into(),
        tmp_dir: matches
            .value_of("tmp_dir")
            .or(env::var("TMPDIR").ok().as_ref().map(|x| x.as_str()))
            .unwrap_or("/tmp")
            .into(),
        send_count: num_arg!("send_count"),
        dest_count: num_arg!("destination_count"),
        simultaneous_process_calls: num_arg!("simultaneous_process_calls"),
        catch_up_timeout: num_arg!("catch_up_timeout"),
        generate_receives: !matches.is_present("no_receives"),
        precompute_blocks: matches.is_present("precompute_blocks"),
        output_stats: matches.is_present("stats"),
    };
    if let Err(ref e) = run(params) {
        eprintln!("Error: {}", e);
        for e in e.iter().skip(1) {
            eprintln!("  caused by: {}", e);
        }

        if let Some(backtrace) = e.backtrace() {
            eprintln!("\nBacktrace:\n{:?}", backtrace);
        }

        process::exit(1);
    }
}
