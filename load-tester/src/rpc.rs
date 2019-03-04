use std::fmt;

use futures::{Future, Stream};
use futures::future;

use tokio_core::reactor::Handle;

use hyper::{self, Request, Uri};
use hyper::client::{Connect, HttpConnector};
use hyper::header::{ContentLength, ContentType};

use serde::{Deserialize, Serialize};

use serde_json::{self, Value};

#[derive(Debug)]
pub enum RpcError {
    Http(hyper::Error),
    Json(serde_json::Error),
    /// An error returned from the RPC
    RpcError(Value),
}

impl fmt::Display for RpcError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            RpcError::Http(ref err) => fmt::Display::fmt(err, f),
            RpcError::Json(ref err) => fmt::Display::fmt(err, f),
            RpcError::RpcError(ref val) => if let Some(s) = val.as_str() {
                fmt::Display::fmt(s, f)
            } else {
                fmt::Display::fmt(val, f)
            },
        }
    }
}

impl ::std::error::Error for RpcError {
    fn description(&self) -> &str {
        match *self {
            RpcError::Http(_) => "HTTP error",
            RpcError::Json(_) => "JSON parsing error",
            RpcError::RpcError(_) => "RPC returned error",
        }
    }
}

pub struct RpcClient<C: Connect = HttpConnector> {
    http_client: hyper::Client<C>,
    uri: Uri,
}

impl RpcClient<HttpConnector> {
    pub fn new(tokio_handle: Handle, rpc_uri: Uri) -> RpcClient<HttpConnector> {
        RpcClient::from_hyper_client(hyper::Client::new(&tokio_handle), rpc_uri)
    }
}

impl<C: Connect> RpcClient<C> {
    pub fn from_hyper_client(hyper_client: hyper::Client<C>, rpc_uri: Uri) -> RpcClient<C> {
        RpcClient {
            http_client: hyper_client,
            uri: rpc_uri,
        }
    }

    pub fn call<'a, I: Serialize, O: Deserialize<'a> + 'static>(
        &self,
        request: &'a I,
    ) -> Box<Future<Item = O, Error = RpcError>> {
        let json = match serde_json::to_vec(request).map_err(RpcError::Json) {
            Ok(json) => json,
            Err(err) => return Box::new(future::err(err)) as _,
        };
        let mut req = Request::new(hyper::Method::Post, self.uri.clone());
        req.headers_mut().set(ContentType::json());
        req.headers_mut().set(ContentLength(json.len() as u64));
        req.set_body(json);
        Box::new(
            self.http_client
                .request(req)
                .and_then(|res| res.body().concat2())
                .map_err(RpcError::Http)
                .and_then(|slice| serde_json::from_slice(&slice).map_err(RpcError::Json))
                .and_then(|mut json: Value| {
                    if let Some(obj) = json.as_object_mut() {
                        if let Some(err) = obj.remove("error") {
                            return Err(RpcError::RpcError(err));
                        }
                    }
                    O::deserialize(json).map_err(RpcError::Json)
                }),
        ) as _
    }
}
