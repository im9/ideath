pub struct Seq {
  pub steps: [i32; 16],
}

// TODO 実装
impl Seq {
  pub fn new() -> Seq {
    Seq {
      steps: Default::default(),
    }
  }
}
