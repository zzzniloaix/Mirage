// filehash: SHA-256 of data and files, checked against the FIPS 180-2 examples.

#include <catch2/catch_test_macros.hpp>

#include "FileHash.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

fs::path write_file(const char* name, const std::string& data)
{
    const fs::path p = fs::temp_directory_path() / name;
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    return p;
}

} // namespace

TEST_CASE("filehash::sha256_hex: the FIPS 180-2 examples", "[filehash]")
{
    CHECK(filehash::sha256_hex("") ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(filehash::sha256_hex("abc") ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(filehash::sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("filehash::sha256_file: a file's contents, also over several reads", "[filehash]")
{
    std::string err;
    const fs::path abc = write_file("mirage_test_hash_abc.txt", "abc");
    CHECK(filehash::sha256_file(abc.string(), err) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(err.empty());

    std::string big;
    for (int i = 0; i < 200000; ++i) big += static_cast<char>((i * 7 + i / 251) & 0xff);
    const fs::path large = write_file("mirage_test_hash_large.bin", big);
    CHECK(filehash::sha256_file(large.string(), err) == filehash::sha256_hex(big));

    const fs::path empty = write_file("mirage_test_hash_empty.txt", "");
    CHECK(filehash::sha256_file(empty.string(), err) == filehash::sha256_hex(""));

    fs::remove(abc);
    fs::remove(large);
    fs::remove(empty);
}

TEST_CASE("filehash::sha256_file: a file that cannot be read", "[filehash]")
{
    const fs::path missing = fs::temp_directory_path() / "mirage_test_hash_missing.txt";
    fs::remove(missing);
    std::string err;
    CHECK(filehash::sha256_file(missing.string(), err).empty());
    CHECK_FALSE(err.empty());

    err.clear();
    CHECK(filehash::sha256_file(fs::temp_directory_path().string(), err).empty());   // a directory
    CHECK_FALSE(err.empty());
}
