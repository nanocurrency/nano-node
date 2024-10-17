#[derive(Clone)]
pub struct ContainerInfo {
    pub name: String,
    pub count: usize,
    pub sizeof_element: usize,
}

#[derive(Clone)]
pub enum ContainerInfoComponent {
    Leaf(ContainerInfo),
    Composite(String, Vec<ContainerInfoComponent>),
}
