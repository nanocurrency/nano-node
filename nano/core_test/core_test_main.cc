
extern void force_nano_test_network ();

#include "gtest/gtest.h"
GTEST_API_ int main(int argc, char **argv) {
  printf("Running main() from core_test_main.cc\n");
  force_nano_test_network ();
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}