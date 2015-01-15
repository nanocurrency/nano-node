#include <gtest/gtest.h>
#include <rai/node.hpp>

TEST (work_store, init)
{
    bool init;
    rai::work_store store (init, boost::filesystem::unique_path ());
    ASSERT_FALSE (init);
}

TEST (work_store, retrieve)
{
    bool init;
    rai::work_store store (init, boost::filesystem::unique_path ());
    ASSERT_FALSE (init);
    rai::keypair key;
    uint64_t work1;
    ASSERT_TRUE (store.get (key.pub, work1));
    store.put (key.pub, work1);
    uint64_t work2;
    ASSERT_FALSE (store.get (key.pub, work2));
    ASSERT_EQ (work1, work2);
}