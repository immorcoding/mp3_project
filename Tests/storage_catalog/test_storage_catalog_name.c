/**
 * @file test_storage_catalog_name.c
 * @brief 曲目显示名纯函数的主机测试：只经 storage_catalog_display_name 观察输出。
 */

#include "APP/tasks/storage/catalog/storage_catalog_name.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/**
 * @brief 断言路径提取成功且结果等于期望。
 * @param[in] path 曲库相对路径。
 * @param[in] expected 期望的显示名。
 */
static void expect_name(const char *path, const char *expected)
{
    char out[STORAGE_CATALOG_NAME_BYTES];

    memset(out, 0x5A, sizeof(out));
    assert(storage_catalog_display_name(path, (uint32_t)strlen(path), out, sizeof(out)));
    assert(strcmp(out, expected) == 0);
}

/**
 * @brief 断言路径提取失败，且输出缓冲被置为空串。
 * @param[in] path 曲库相对路径。
 * @param[in] path_bytes 路径字节数。
 */
static void expect_failure(const char *path, uint32_t path_bytes)
{
    char out[STORAGE_CATALOG_NAME_BYTES];

    memset(out, 0x5A, sizeof(out));
    assert(!storage_catalog_display_name(path, path_bytes, out, sizeof(out)));
    assert(out[0] == '\0');
}

/**
 * @brief 生成 "Music/" + n 个 'a' + ".mp3"。
 * @param[out] buf 至少 n + 11 字节。
 * @param[in] n 主名字节数。
 * @return 路径字节数。
 */
static uint32_t make_long_path(char *buf, uint32_t n)
{
    memcpy(buf, "Music/", 6U);
    memset(buf + 6U, 'a', n);
    memcpy(buf + 6U + n, ".mp3", 5U);
    return 6U + n + 4U;
}

/** @brief 扩展名只去末尾一个，大小写与扫描接受的形式一致。 */
static void test_extension_rules(void)
{
    expect_name("Music/a.b.MP3", "a.b");
    expect_name("Music/a.mp3.mp3", "a.mp3");
    expect_name("Music/a.Mp3", "a");
    expect_name("Music/a.mP3", "a");
    expect_name("a.mp3", "a");
    expect_name("Music/a.mp4", "a.mp4");
    expect_name("Music/a.mp3x", "a.mp3x");
    expect_name("Music/noext", "noext");
}

/** @brief 中文、混排与前导点号原样保留。 */
static void test_names_preserved(void)
{
    expect_name("Music/01 - \xE6\x99\xB4\xE5\xA4\xA9.mp3", "01 - \xE6\x99\xB4\xE5\xA4\xA9");
    expect_name("Music/Jay \xE5\x91\xA8\xE6\x9D\xB0\xE4\xBC\xA6 - x.mp3", "Jay \xE5\x91\xA8\xE6\x9D\xB0\xE4\xBC\xA6 - x");
    expect_name("Music/.hidden.mp3", ".hidden");
    expect_name("A/B/C/deep.mp3", "deep");
}

/** @brief 只有扩展名的文件保留整名，避免空显示名。 */
static void test_extension_only_name(void)
{
    expect_name("Music/.mp3", ".mp3");
}

/** @brief 空路径、以斜杠结尾的路径、空指针与缓冲不足都失败。 */
static void test_degenerate_inputs(void)
{
    char out[STORAGE_CATALOG_NAME_BYTES];

    expect_failure("", 0U);
    expect_failure("Music/", 6U);
    assert(!storage_catalog_display_name(NULL, 4U, out, sizeof(out)));
    assert(!storage_catalog_display_name("a.mp3", 5U, NULL, sizeof(out)));
    assert(!storage_catalog_display_name("a.mp3", 5U, out, 0U));
    assert(!storage_catalog_display_name("ab.mp3", 6U, out, 2U));
    assert(storage_catalog_display_name("ab.mp3", 6U, out, 3U));
    assert(strcmp(out, "ab") == 0);
}

/** @brief 恰好 251 B 成功，252 B 失败且不截断；多字节字符跨边界不写半个。 */
static void test_capacity_boundary(void)
{
    char path[6U + 260U + 5U];
    char out[STORAGE_CATALOG_NAME_BYTES];
    uint32_t length;

    length = make_long_path(path, 251U);
    assert(storage_catalog_display_name(path, length, out, sizeof(out)));
    assert(strlen(out) == 251U);
    assert(out[250] == 'a');

    length = make_long_path(path, 252U);
    expect_failure(path, length);

    /* 248 个 'a' + 3 字节汉字 = 251 B，恰好放下。 */
    memcpy(path, "Music/", 6U);
    memset(path + 6U, 'a', 248U);
    memcpy(path + 6U + 248U, "\xE6\x99\xB4", 3U);
    memcpy(path + 6U + 251U, ".mp3", 4U);
    length = 6U + 251U + 4U;
    assert(storage_catalog_display_name(path, length, out, sizeof(out)));
    assert(strlen(out) == 251U);
    assert(memcmp(out + 248, "\xE6\x99\xB4", 4U) == 0);

    /* 250 个 'a' + 3 字节汉字 = 253 B，放不下，汉字不被切成半个。 */
    memset(path + 6U, 'a', 250U);
    memcpy(path + 6U + 250U, "\xE6\x99\xB4", 3U);
    memcpy(path + 6U + 253U, ".mp3", 4U);
    expect_failure(path, 6U + 253U + 4U);
}

/** @brief 非法 UTF-8 一律失败。 */
static void test_invalid_utf8(void)
{
    expect_failure("Music/\x80.mp3", 11U);             /* 孤立续字节 */
    expect_failure("Music/\xC3.mp3", 11U);             /* 截断的 2 字节序列 */
    expect_failure("Music/\xC0\x80.mp3", 12U);         /* 过长编码 */
    expect_failure("Music/\xE6\x99.mp3", 12U);         /* 截断的 3 字节序列 */
    expect_failure("Music/\xED\xA0\x80.mp3", 13U);     /* 代理项 */
    expect_failure("Music/\xF4\x90\x80\x80.mp3", 14U); /* 超出 U+10FFFF */
    expect_failure("Music/\xFF.mp3", 11U);             /* 非法首字节 */
    expect_failure("Music/a\0b.mp3", 13U);             /* 主名内嵌结尾符 */
    expect_name("Music/\xF0\x9F\x8E\xB5.mp3", "\xF0\x9F\x8E\xB5"); /* 合法 4 字节 */
}

/**
 * @brief 运行全部用例。
 * @return 0 表示全部通过（失败由 assert 中止）。
 */
int main(void)
{
    test_extension_rules();
    test_names_preserved();
    test_extension_only_name();
    test_degenerate_inputs();
    test_capacity_boundary();
    test_invalid_utf8();
    puts("storage_catalog_name: ok");
    return 0;
}
