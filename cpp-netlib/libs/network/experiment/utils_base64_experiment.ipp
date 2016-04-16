class test_name {
  void test_single_block();
  void test_multiple_blocks();

 public:
  test_name() {
    std::cout << "Executing " stringify(test_name) ":" << std::endl;
    test_single_block();
    test_multiple_blocks();
  }
};

test_name concatenate(run_, test_name);

void test_name::test_single_block() {
  std::cout << "  Encoding " << single_block_size << " MB buffer took ";
  // Fill a single vector with random bytes.
  std::vector<unsigned char> buffer(single_block_size * 1024 * 1024);
  for (std::vector<unsigned char>::iterator current = buffer.begin();
       current != buffer.end(); ++current)
    *current = static_cast<unsigned char>(rand() % 255);
  // Encode the single vector to a single BASE64 string.
  clock_t start = clock();
  encode_single_block;
  clock_t end = clock();
  std::cout << (double)(end - start) / CLOCKS_PER_SEC << "s." << std::endl;
}

void test_name::test_multiple_blocks() {
#ifdef base64_with_state
  std::cout << "  Encoding " << multiple_block_count << " x "
            << multiple_block_size << " KB buffers took ";
  // Fill multiple vectors with random bytes.
  std::vector<std::vector<unsigned char> > buffers(multiple_block_count);
  for (unsigned block_index = 0; block_index < buffers.size(); ++block_index) {
    std::vector<unsigned char>& buffer = buffers[block_index];
    buffer.resize(multiple_block_size * 1024);
    for (std::vector<unsigned char>::iterator current = buffer.begin();
         current != buffer.end(); ++current)
      *current = static_cast<unsigned char>(rand() % 255);
  }
  // Encode the multiple vectors to a single BASE64 string.
  clock_t start = clock();
  encode_multiple_blocks;
  clock_t end = clock();
  std::cout << (double)(end - start) / CLOCKS_PER_SEC << "s." << std::endl;
#endif
}
