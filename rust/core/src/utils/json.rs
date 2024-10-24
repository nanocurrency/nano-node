use serde_json::{Map, Value};
use std::any::Any;
use std::collections::HashMap;

pub trait PropertyTree {
    fn get_string(&self, path: &str) -> anyhow::Result<String>;
    fn get_bool(&self, path: &str, default_value: bool) -> bool {
        self.get_string(path)
            .map(|s| s == "true")
            .unwrap_or(default_value)
    }
    fn get_child(&self, path: &str) -> Option<Box<dyn PropertyTree>>;
    fn get_children(&self) -> Vec<(String, Box<dyn PropertyTree>)>;
    fn data(&self) -> String;
    fn clear(&mut self) -> anyhow::Result<()>;
    fn put_string(&mut self, path: &str, value: &str) -> anyhow::Result<()>;
    fn put_u64(&mut self, path: &str, value: u64) -> anyhow::Result<()>;
    fn new_writer(&self) -> Box<dyn PropertyTree>;
    fn push_back(&mut self, path: &str, value: &dyn PropertyTree);
    fn add_child(&mut self, path: &str, value: &dyn PropertyTree);
    fn put_child(&mut self, path: &str, value: &dyn PropertyTree);
    fn add(&mut self, path: &str, value: &str) -> anyhow::Result<()>;
    fn as_any(&self) -> &dyn Any;
    fn as_any_mut(&mut self) -> &mut dyn Any;
    fn to_json(&self) -> String;
}

pub trait PropertyTreeWriter {}

pub struct TestPropertyTree {
    properties: HashMap<String, String>,
}

impl TestPropertyTree {
    pub fn new() -> Self {
        Self {
            properties: HashMap::new(),
        }
    }
}

impl PropertyTree for TestPropertyTree {
    fn get_string(&self, path: &str) -> anyhow::Result<String> {
        self.properties
            .get(path)
            .cloned()
            .ok_or_else(|| anyhow!("path not found"))
    }

    fn get_child(&self, _path: &str) -> Option<Box<dyn PropertyTree>> {
        unimplemented!()
    }

    fn get_children(&self) -> Vec<(String, Box<dyn PropertyTree>)> {
        unimplemented!()
    }

    fn data(&self) -> String {
        unimplemented!()
    }

    fn put_string(&mut self, path: &str, value: &str) -> anyhow::Result<()> {
        self.properties.insert(path.to_owned(), value.to_owned());
        Ok(())
    }

    fn put_u64(&mut self, _path: &str, _value: u64) -> anyhow::Result<()> {
        todo!()
    }

    fn new_writer(&self) -> Box<dyn PropertyTree> {
        todo!()
    }

    fn push_back(&mut self, _path: &str, _value: &dyn PropertyTree) {
        todo!()
    }

    fn add_child(&mut self, _path: &str, _value: &dyn PropertyTree) {
        todo!()
    }

    fn add(&mut self, _path: &str, _value: &str) -> anyhow::Result<()> {
        todo!()
    }

    fn as_any(&self) -> &dyn Any {
        self
    }

    fn as_any_mut(&mut self) -> &mut dyn Any {
        self
    }

    fn clear(&mut self) -> anyhow::Result<()> {
        todo!()
    }

    fn put_child(&mut self, _path: &str, _value: &dyn PropertyTree) {
        todo!()
    }

    fn to_json(&self) -> String {
        todo!()
    }
}

pub fn as_nano_json(value: bool) -> &'static str {
    if value {
        "true"
    } else {
        "false"
    }
}

#[derive(Clone)]
pub struct SerdePropertyTree {
    pub value: Value,
}

impl SerdePropertyTree {
    pub fn new() -> Self {
        Self {
            value: Value::Object(Map::new()),
        }
    }

    pub fn from_value(value: Value) -> Self {
        Self { value }
    }

    pub fn parse(s: &str) -> anyhow::Result<Self> {
        Ok(Self {
            value: serde_json::from_str(s)?,
        })
    }

    pub fn add_child_value(&mut self, path: String, value: Value) {
        let Value::Object(map) = &mut self.value else {
            panic!("not an object");
        };
        map.insert(path, value);
    }
}

impl PropertyTree for SerdePropertyTree {
    fn get_string(&self, path: &str) -> anyhow::Result<String> {
        match self.value.get(path) {
            Some(v) => match v {
                serde_json::Value::String(s) => Ok(s.to_owned()),
                _ => Err(anyhow!("not a string value")),
            },
            None => Err(anyhow!("could not find path")),
        }
    }

    fn get_child(&self, path: &str) -> Option<Box<dyn PropertyTree>> {
        self.value.get(path).map(|value| {
            let child: Box<dyn PropertyTree> = Box::new(Self {
                value: value.clone(),
            });
            child
        })
    }

    fn get_children(&self) -> Vec<(String, Box<dyn PropertyTree>)> {
        match &self.value {
            Value::Array(array) => array
                .iter()
                .map(|i| {
                    let reader: Box<dyn PropertyTree> =
                        Box::new(SerdePropertyTree { value: i.clone() });
                    (String::default(), reader)
                })
                .collect(),
            Value::Object(object) => object
                .iter()
                .map(|(k, v)| {
                    let reader: Box<dyn PropertyTree> =
                        Box::new(SerdePropertyTree { value: v.clone() });
                    (k.clone(), reader)
                })
                .collect(),
            _ => Vec::new(),
        }
    }

    fn data(&self) -> String {
        match &self.value {
            Value::String(s) => s.clone(),
            _ => unimplemented!(),
        }
    }

    fn clear(&mut self) -> anyhow::Result<()> {
        self.value = Value::Object(Map::new());
        Ok(())
    }

    fn put_string(&mut self, path: &str, value: &str) -> anyhow::Result<()> {
        let Value::Object(map) = &mut self.value else {
            bail!("not an object")
        };
        map.insert(path.to_string(), Value::String(value.to_string()));
        Ok(())
    }

    fn put_u64(&mut self, path: &str, value: u64) -> anyhow::Result<()> {
        let Value::Object(map) = &mut self.value else {
            bail!("not an object")
        };
        map.insert(path.to_string(), Value::Number(value.into()));
        Ok(())
    }

    fn new_writer(&self) -> Box<dyn PropertyTree> {
        Box::new(Self::new())
    }

    fn push_back(&mut self, _path: &str, _value: &dyn PropertyTree) {
        todo!()
    }

    fn add_child(&mut self, path: &str, value: &dyn PropertyTree) {
        let child = value
            .as_any()
            .downcast_ref::<SerdePropertyTree>()
            .expect("not a serde ptree");

        let Value::Object(map) = &mut self.value else {
            panic!("not an object");
        };
        map.insert(path.to_string(), child.value.clone());
    }

    fn put_child(&mut self, _path: &str, _value: &dyn PropertyTree) {
        todo!()
    }

    fn add(&mut self, path: &str, value: &str) -> anyhow::Result<()> {
        self.put_string(path, value)
    }

    fn as_any(&self) -> &dyn Any {
        self
    }

    fn as_any_mut(&mut self) -> &mut dyn Any {
        self
    }

    fn to_json(&self) -> String {
        self.value.to_string()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn property_not_found() {
        let tree = TestPropertyTree::new();
        assert!(tree.get_string("DoesNotExist").is_err());
    }

    #[test]
    fn set_string_property() {
        let mut tree = TestPropertyTree::new();
        tree.put_string("foo", "bar").unwrap();
        assert_eq!(tree.get_string("foo").unwrap(), "bar");
    }
}
