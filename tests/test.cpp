#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

static std::string readFile(const fs::path &path) {
    std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

static void writeFile(const fs::path &path, const std::string &content) {
    std::ofstream file(path);
    file << content;
}

// Путь к бинарнику утилиты задаётся в CMake
static const char *kRefactorToolBinary = REFACTOR_TOOL_BINARY;

static int runRefactorTool(const fs::path &filePath) {
    std::string cmd = std::string(kRefactorToolBinary) + " " + filePath.string() + " -- > /dev/null 2>&1";
    return std::system(cmd.c_str());
}

class RefactorToolTest : public ::testing::Test {
protected:
    fs::path tmpDir = fs::temp_directory_path() / "refactor_tool_tests";

    void SetUp() override { fs::create_directories(tmpDir); }

    void TearDown() override {
        if (!HasFailure()) {
            fs::remove_all(tmpDir);
        }
    }
};

// ============================================================================
// Тесты для виртуальных деструкторов
// ============================================================================

TEST_F(RefactorToolTest, VirtualDtor_BaseWithDerived) {
    fs::path file = tmpDir / "vd1.cpp";
    writeFile(file, "class Base { public: ~Base() {} };\nclass Derived : public Base {};\n");
    ASSERT_EQ(runRefactorTool(file), 0);
    std::string result = readFile(file);
    EXPECT_NE(result.find("virtual ~Base()"), std::string::npos);
}

TEST_F(RefactorToolTest, VirtualDtor_NoDerived) {
    fs::path file = tmpDir / "vd2.cpp";
    writeFile(file, "class Standalone { public: ~Standalone() {} };\n");
    ASSERT_EQ(runRefactorTool(file), 0);
    std::string result = readFile(file);
    EXPECT_EQ(result.find("virtual"), std::string::npos);
}

TEST_F(RefactorToolTest, VirtualDtor_AlreadyVirtual) {
    fs::path file = tmpDir / "vd3.cpp";
    writeFile(file, "class Base { public: virtual ~Base() {} };\nclass Derived : public Base {};\n");
    ASSERT_EQ(runRefactorTool(file), 0);
    std::string result = readFile(file);
    size_t pos = result.find("virtual");
    EXPECT_EQ(result.find("virtual", pos + 1), std::string::npos);
}

// ============================================================================
// Тесты для override
// ============================================================================

TEST_F(RefactorToolTest, Override_Missing) {
    fs::path file = tmpDir / "ov1.cpp";
    writeFile(file, "class Base { public: virtual void func() {} };\n"
                    "class Derived : public Base { public: void func() {} };\n");
    ASSERT_EQ(runRefactorTool(file), 0);
    std::string result = readFile(file);
    EXPECT_NE(result.find("void func() override"), std::string::npos);
}

TEST_F(RefactorToolTest, Override_AlreadyPresent) {
    fs::path file = tmpDir / "ov2.cpp";
    writeFile(file, "class Base { public: virtual void func() {} };\n"
                    "class Derived : public Base { public: void func() override {} };\n");
    ASSERT_EQ(runRefactorTool(file), 0);
    std::string result = readFile(file);
    EXPECT_EQ(result.find("override override"), std::string::npos);
}

// ============================================================================
// Тесты для range-for
// ============================================================================

TEST_F(RefactorToolTest, RangeFor_AddAmpersand) {
    fs::path file = tmpDir / "rf1.cpp";
    writeFile(file, "#include <vector>\nstruct S { int x; };\n"
                    "void f() { std::vector<S> v; for (const auto x : v) {} }\n");
    ASSERT_EQ(runRefactorTool(file), 0);
    std::string result = readFile(file);
    EXPECT_NE(result.find("const auto& x"), std::string::npos);
}

TEST_F(RefactorToolTest, RangeFor_FundamentalType) {
    fs::path file = tmpDir / "rf2.cpp";
    writeFile(file, "#include <vector>\n"
                    "void f() { std::vector<int> v; for (const int x : v) {} }\n");
    ASSERT_EQ(runRefactorTool(file), 0);
    std::string result = readFile(file);
    EXPECT_EQ(result.find("const int& x"), std::string::npos);
}

TEST_F(RefactorToolTest, RangeFor_AlreadyReference) {
    fs::path file = tmpDir / "rf3.cpp";
    writeFile(file, "#include <vector>\nstruct S { int x; };\n"
                    "void f() { std::vector<S> v; for (const auto& x : v) {} }\n");
    ASSERT_EQ(runRefactorTool(file), 0);
    std::string result = readFile(file);
    EXPECT_EQ(result.find("const auto&& x"), std::string::npos);
}