/**
 * @file test_storage_catalog_text.c
 * @brief 曲目文本纯函数的主机测试：只经 storage_catalog_track_text 观察输出。
 * @note  假文件由字节数组构造，GBK 查表链接真实 cc936.c；id3_reader 只经此接缝被测。
 */

#include "APP/tasks/storage/catalog/storage_catalog_text.h"

#include "ff.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define FAKE_FILE_MAX 65536U
#define FAKE_READ_LOG_MAX 16U

/** @brief 假文件：字节数组加读取记录与错误注入。 */
typedef struct
{
    uint8_t data[FAKE_FILE_MAX]; /**< 文件内容。 */
    uint32_t length;             /**< 文件长度。 */
    uint32_t reads;              /**< 回调被调用次数。 */
    uint32_t read_offset[FAKE_READ_LOG_MAX]; /**< 每次读的偏移。 */
    uint32_t read_bytes[FAKE_READ_LOG_MAX];  /**< 每次读的请求字节数。 */
    uint32_t fail_at;            /**< 第几次读返回错误（从 1 数）；0 表示不注入。 */
} fake_file_t;

static fake_file_t g_file;
static uint8_t g_block[STORAGE_CATALOG_TEXT_BLOCK_BYTES];

/**
 * @brief 假文件读回调：按偏移拷贝，越过文件尾则短读。
 * @param[in] context fake_file_t 指针。
 * @param[in] offset 文件偏移。
 * @param[out] buffer 输出缓冲。
 * @param[in] bytes 请求字节数。
 * @param[out] read_bytes 实际字节数。
 * @return false 表示注入的读取错误。
 */
static bool fake_read(void *context, uint32_t offset, uint8_t *buffer, uint32_t bytes, uint32_t *read_bytes)
{
    fake_file_t *file = (fake_file_t *)context;
    uint32_t n = 0U;

    if (file->reads < FAKE_READ_LOG_MAX)
    {
        file->read_offset[file->reads] = offset;
        file->read_bytes[file->reads] = bytes;
    }
    file->reads++;
    if ((file->fail_at != 0U) && (file->reads == file->fail_at))
    {
        *read_bytes = 0U;
        return false;
    }
    if (offset < file->length)
    {
        n = file->length - offset;
        if (n > bytes)
        {
            n = bytes;
        }
        memcpy(buffer, file->data + offset, n);
    }
    *read_bytes = n;
    return true;
}

/**
 * @brief 用真实 cc936 表实现 GBK → Unicode 查表回调。
 * @param[in] gbk_code GBK 双字节码。
 * @param[in] context 未使用。
 * @return Unicode 码点，0 表示无映射。
 */
static uint16_t gbk_lookup(uint16_t gbk_code, void *context)
{
    (void)context;
    return (uint16_t)ff_convert((WCHAR)gbk_code, 1U);
}

/** @brief 清空假文件。 */
static void file_reset(void)
{
    memset(&g_file, 0, sizeof(g_file));
}

/**
 * @brief 追加原始字节。
 * @param[in] bytes 字节。
 * @param[in] n 字节数。
 */
static void put(const void *bytes, uint32_t n)
{
    assert(g_file.length + n <= FAKE_FILE_MAX);
    memcpy(g_file.data + g_file.length, bytes, n);
    g_file.length += n;
}

/**
 * @brief 追加 ID3 标签头，大小字段待 tag_end 回填。
 * @param[in] major 主版本。
 * @param[in] flags 标签标志。
 */
static void tag_begin(uint8_t major, uint8_t flags)
{
    const uint8_t header[10] = {'I', 'D', '3', major, 0U, flags, 0U, 0U, 0U, 0U};

    put(header, sizeof(header));
}

/**
 * @brief 追加一个帧。
 * @param[in] major 标签主版本，决定帧头格式。
 * @param[in] id 帧 ID（v2.2 为 3 字符，否则 4 字符）。
 * @param[in] flags 帧标志（v2.3/v2.4 的 16 位，高字节为状态标志）。
 * @param[in] payload 负载。
 * @param[in] n 负载字节数。
 */
static void frame(uint8_t major, const char *id, uint16_t flags, const void *payload, uint32_t n)
{
    uint8_t header[10];
    uint32_t id_len = (major == 2U) ? 3U : 4U;

    memcpy(header, id, id_len);
    if (major == 2U)
    {
        header[3] = (uint8_t)(n >> 16);
        header[4] = (uint8_t)(n >> 8);
        header[5] = (uint8_t)n;
        put(header, 6U);
    }
    else
    {
        if (major == 3U)
        {
            header[4] = (uint8_t)(n >> 24);
            header[5] = (uint8_t)(n >> 16);
            header[6] = (uint8_t)(n >> 8);
            header[7] = (uint8_t)n;
        }
        else
        {
            header[4] = (uint8_t)((n >> 21) & 0x7FU);
            header[5] = (uint8_t)((n >> 14) & 0x7FU);
            header[6] = (uint8_t)((n >> 7) & 0x7FU);
            header[7] = (uint8_t)(n & 0x7FU);
        }
        header[8] = (uint8_t)(flags >> 8);
        header[9] = (uint8_t)flags;
        put(header, 10U);
    }
    put(payload, n);
}

/**
 * @brief 追加零填充并回填标签大小（syncsafe）。
 * @param[in] padding 填充字节数。
 */
static void tag_end(uint32_t padding)
{
    uint32_t size;

    assert(g_file.length + padding <= FAKE_FILE_MAX);
    memset(g_file.data + g_file.length, 0, padding);
    g_file.length += padding;
    size = g_file.length - 10U;
    g_file.data[6] = (uint8_t)((size >> 21) & 0x7FU);
    g_file.data[7] = (uint8_t)((size >> 14) & 0x7FU);
    g_file.data[8] = (uint8_t)((size >> 7) & 0x7FU);
    g_file.data[9] = (uint8_t)(size & 0x7FU);
}

/**
 * @brief 追加 n 字节伪音频数据（0xFF 0xFB 开头的帧同步样式）。
 * @param[in] n 字节数。
 */
static void audio(uint32_t n)
{
    assert(g_file.length + n <= FAKE_FILE_MAX);
    for (uint32_t i = 0U; i < n; i++)
    {
        g_file.data[g_file.length + i] = (uint8_t)((i & 1U) ? 0xFBU : 0xFFU);
    }
    g_file.length += n;
}

/**
 * @brief 对当前假文件运行曲目文本函数。
 * @param[out] text 输出。
 */
static void run(StorageCatalog_TextTypeDef *text)
{
    static const char path[] = "Music/Dir/stem name.mp3";
    StorageCatalog_TextRequestTypeDef request = {
        path, (uint32_t)(sizeof(path) - 1U), g_file.length, fake_read, &g_file, gbk_lookup, NULL, g_block};

    g_file.reads = 0U;
    memset(text, 0x5A, sizeof(*text));
    assert(storage_catalog_track_text(&request, text));
}

/**
 * @brief 断言结果分类、标题、歌手。
 * @param[in] text 输出。
 * @param[in] result 期望分类。
 * @param[in] title 期望标题。
 * @param[in] artist 期望歌手。
 */
static void expect(const StorageCatalog_TextTypeDef *text,
                   StorageCatalog_TextResultTypeDef result,
                   const char *title,
                   const char *artist)
{
    if ((text->Result != result) || (strcmp(text->Title, title) != 0) || (strcmp(text->Artist, artist) != 0))
    {
        for (const char *q = text->Title; *q; q++) fprintf(stderr, "%02X ", (unsigned char)*q);
        fprintf(stderr, "result=%d (want %d) title=[%s] (want [%s]) artist=[%s] (want [%s])\n",
               (int)text->Result, (int)result, text->Title, title, text->Artist, artist);
        assert(0);
    }
}

/** @brief 取到：v2.3 UTF-16LE 带 BOM 的标题与歌手；首块一次读完。 */
static void test_v23_utf16le_title_and_artist(void)
{
    StorageCatalog_TextTypeDef text;
    /* "Hi" 与 "Bob"，编码 1，FF FE 开头。 */
    const uint8_t tit2[] = {1, 0xFF, 0xFE, 'H', 0, 'i', 0};
    const uint8_t tpe1[] = {1, 0xFF, 0xFE, 'B', 0, 'o', 0, 'b', 0};

    file_reset();
    tag_begin(3U, 0U);
    frame(3U, "TIT2", 0U, tit2, sizeof(tit2));
    frame(3U, "TPE1", 0U, tpe1, sizeof(tpe1));
    tag_end(0U);
    audio(100U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_TAGGED, "Hi", "Bob");
    assert(text.Reads == 1U);
}

#define RUN_TEST(t) do { fprintf(stderr, "%s\n", #t); t(); } while (0)
#define STEM "stem name"
#define QING_TIAN "\xE6\x99\xB4\xE5\xA4\xA9" /* 晴天 的 UTF-8。 */

/** @brief 文本来源的 BOM 形式。 */
typedef enum
{
    BOM_NONE = 0,
    BOM_LE,
    BOM_BE
} bom_t;

/**
 * @brief 构造 [编码字节][文本] 的负载，文本由 ASCII 展开。
 * @param[out] out 输出，至少 64 字节。
 * @param[in] enc 编码字节。
 * @param[in] bom BOM 形式（仅编码 1 使用）。
 * @param[in] big_endian 单元字节序。
 * @param[in] ascii ASCII 文本；其中 '|' 被展开为 NUL 分隔。
 * @return 负载字节数。
 */
static uint32_t enc_text(uint8_t *out, uint8_t enc, bom_t bom, bool big_endian, const char *ascii)
{
    uint32_t n = 0U;

    out[n++] = enc;
    if (bom == BOM_LE)
    {
        out[n++] = 0xFFU;
        out[n++] = 0xFEU;
    }
    else if (bom == BOM_BE)
    {
        out[n++] = 0xFEU;
        out[n++] = 0xFFU;
    }
    for (; *ascii != '\0'; ascii++)
    {
        uint8_t c = (*ascii == '|') ? 0U : (uint8_t)*ascii;

        if ((enc == 1U) || (enc == 2U))
        {
            out[n++] = big_endian ? 0U : c;
            out[n++] = big_endian ? c : 0U;
        }
        else
        {
            out[n++] = c;
        }
    }
    return n;
}

/**
 * @brief 用单个 TIT2 帧（v2.3，原始负载）构造文件并运行。
 * @param[in] payload 含编码字节的负载。
 * @param[in] n 负载字节数。
 * @param[out] text 输出。
 */
static void run_title_payload(const void *payload, uint32_t n, StorageCatalog_TextTypeDef *text)
{
    file_reset();
    tag_begin(3U, 0U);
    frame(3U, "TIT2", 0U, payload, n);
    tag_end(0U);
    audio(64U);
    run(text);
}

/**
 * @brief 断言单个 TIT2 负载解出期望的标题（分类为取到，歌手为空）。
 * @param[in] payload 含编码字节的负载。
 * @param[in] n 负载字节数。
 * @param[in] title 期望标题。
 */
static void expect_title(const void *payload, uint32_t n, const char *title)
{
    StorageCatalog_TextTypeDef text;

    run_title_payload(payload, n, &text);
    expect(&text, STORAGE_CATALOG_TEXT_TAGGED, title, "");
}

/**
 * @brief 断言单个 TIT2 负载解码失败并回退文件主名。
 * @param[in] payload 含编码字节的负载。
 * @param[in] n 负载字节数。
 */
static void expect_title_failure(const void *payload, uint32_t n)
{
    StorageCatalog_TextTypeDef text;

    run_title_payload(payload, n, &text);
    expect(&text, STORAGE_CATALOG_TEXT_DECODE_FAILED, STEM, "");
}

/**
 * @brief 断言单个 TIT2 负载清理后为空，分类帧缺失并回退文件主名。
 * @param[in] payload 含编码字节的负载。
 * @param[in] n 负载字节数。
 */
static void expect_title_empty(const void *payload, uint32_t n)
{
    StorageCatalog_TextTypeDef text;

    run_title_payload(payload, n, &text);
    expect(&text, STORAGE_CATALOG_TEXT_FRAME_MISSING, STEM, "");
}

/** @brief v2.2 的 TT2/TP1 三字符帧与 Latin-1。 */
static void test_v22_frames(void)
{
    StorageCatalog_TextTypeDef text;
    const uint8_t tt2[] = {0, 'S', 'o', 'n', 'g'};
    const uint8_t tp1[] = {0, 'A', 'r', 't'};

    file_reset();
    tag_begin(2U, 0U);
    frame(2U, "TT2", 0U, tt2, sizeof(tt2));
    frame(2U, "TP1", 0U, tp1, sizeof(tp1));
    tag_end(0U);
    audio(32U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_TAGGED, "Song", "Art");
}

/** @brief v2.4：syncsafe 帧大小与 UTF-8，开头 BOM 被去掉。 */
static void test_v24_utf8(void)
{
    StorageCatalog_TextTypeDef text;
    uint8_t tit2[200];
    uint8_t tpe1[] = {3, 0xEF, 0xBB, 0xBF, 'A', 'B'};
    uint32_t n;

    n = enc_text(tit2, 3U, BOM_NONE, false, "x");
    memcpy(tit2 + 1, QING_TIAN, 6U);
    n = 1U + 6U;
    file_reset();
    tag_begin(4U, 0U);
    frame(4U, "TIT2", 0U, tit2, n);
    frame(4U, "TPE1", 0U, tpe1, sizeof(tpe1));
    tag_end(0U);
    audio(32U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_TAGGED, QING_TIAN, "AB");
}

/** @brief 编码 0：真 Latin-1 按 Latin-1 解，声明 Latin-1 实为 GBK 按 GBK 解。 */
static void test_encoding_zero_latin1_and_gbk(void)
{
    const uint8_t latin1[] = {0, 'C', 'a', 'f', 0xE9};
    const uint8_t gbk[] = {0, '0', '1', ' ', 0xC7, 0xE7, 0xCC, 0xEC};
    const uint8_t not_gbk_lead[] = {0, 0xFF, 0xFF};
    const uint8_t unmapped_pair[] = {0, 0xAA, 0xA1}; /* 结构合法但 cc936 无映射的用户区码位。 */
    const uint8_t mixed_invalid[] = {0, 0xC7, 0xE7, 0xE9}; /* 末尾落单高位字节：整体不是 GBK。 */

    expect_title(latin1, sizeof(latin1), "Caf\xC3\xA9");
    expect_title(gbk, sizeof(gbk), "01 " QING_TIAN);
    expect_title(not_gbk_lead, sizeof(not_gbk_lead), "\xC3\xBF\xC3\xBF");
    expect_title(unmapped_pair, sizeof(unmapped_pair), "\xC2\xAA\xC2\xA1");
    expect_title(mixed_invalid, sizeof(mixed_invalid), "\xC3\x87\xC3\xA7\xC3\xA9");
}

/** @brief 编码 0 的 GBK 判定需要查表回调；回调为 NULL 时按 Latin-1。 */
static void test_gbk_needs_lookup(void)
{
    static const char path[] = "Music/a.mp3";
    const uint8_t gbk[] = {0, 0xC7, 0xE7};
    StorageCatalog_TextTypeDef text;
    StorageCatalog_TextRequestTypeDef request = {
        path, (uint32_t)(sizeof(path) - 1U), 0U, fake_read, &g_file, NULL, NULL, g_block};

    file_reset();
    tag_begin(3U, 0U);
    frame(3U, "TIT2", 0U, gbk, sizeof(gbk));
    tag_end(0U);
    request.FileBytes = g_file.length;
    assert(storage_catalog_track_text(&request, &text));
    assert(text.Result == STORAGE_CATALOG_TEXT_TAGGED);
    assert(strcmp(text.Title, "\xC3\x87\xC3\xA7") == 0);
}

/** @brief UTF-16：BOM 各形式、缺 BOM 按 LE、编码 2 为 BE、代理对与中文。 */
static void test_utf16_variants(void)
{
    uint8_t buf[64];
    const uint8_t no_bom_cjk[] = {1, 0x74, 0x66};          /* 缺 BOM，LE：U+6674 晴。 */
    const uint8_t surrogate_pair[] = {1, 0xFF, 0xFE, 0x34, 0xD8, 0x1E, 0xDD}; /* U+1D11E。 */
    const uint8_t bom_only[] = {1, 0xFF, 0xFE};

    expect_title(buf, enc_text(buf, 1U, BOM_LE, false, "Hi"), "Hi");
    expect_title(buf, enc_text(buf, 1U, BOM_BE, true, "Hi"), "Hi");
    expect_title(buf, enc_text(buf, 1U, BOM_NONE, false, "Hi"), "Hi");
    expect_title(buf, enc_text(buf, 2U, BOM_NONE, true, "Hi"), "Hi");
    expect_title(no_bom_cjk, sizeof(no_bom_cjk), "\xE6\x99\xB4");
    expect_title(surrogate_pair, sizeof(surrogate_pair), "\xF0\x9D\x84\x9E");
    expect_title_empty(bom_only, sizeof(bom_only));
}

/** @brief 编码 1 的多值每项各自识别 BOM；缺 BOM 的项按 LE，不继承上一项的字节序。 */
static void test_utf16_bom_per_value(void)
{
    const uint8_t two_boms[] = {1, 0xFF, 0xFE, 'A', 0, 0, 0, 0xFE, 0xFF, 0, 'B'};
    const uint8_t second_without_bom[] = {1, 0xFE, 0xFF, 0, 'A', 0, 0, 'B', 0};

    expect_title(two_boms, sizeof(two_boms), "A / B");
    expect_title(second_without_bom, sizeof(second_without_bom), "A / B");
}

/** @brief UTF-8、UTF-16 的非法内容整帧失败，不替换也不续显。 */
static void test_invalid_text_fails_whole_frame(void)
{
    const uint8_t overlong[] = {3, 'a', 0xC0, 0x80};
    const uint8_t utf8_surrogate[] = {3, 0xED, 0xA0, 0x80};
    const uint8_t stray_continuation[] = {3, 'a', 0x80};
    const uint8_t truncated_sequence[] = {3, 'a', 0xE6, 0x99};
    const uint8_t too_large[] = {3, 0xF4, 0x90, 0x80, 0x80};
    const uint8_t lone_high[] = {1, 0xFF, 0xFE, 0x34, 0xD8, 'a', 0};
    const uint8_t lone_low[] = {1, 0xFF, 0xFE, 0x1E, 0xDD};
    const uint8_t high_at_end[] = {1, 0xFF, 0xFE, 0x34, 0xD8};
    const uint8_t odd_length[] = {1, 0xFF, 0xFE, 'a', 0, 'b'};
    const uint8_t odd_be[] = {2, 0, 'a', 0};
    const uint8_t bad_encoding[] = {4, 'a'};

    expect_title_failure(overlong, sizeof(overlong));
    expect_title_failure(utf8_surrogate, sizeof(utf8_surrogate));
    expect_title_failure(stray_continuation, sizeof(stray_continuation));
    expect_title_failure(truncated_sequence, sizeof(truncated_sequence));
    expect_title_failure(too_large, sizeof(too_large));
    expect_title_failure(lone_high, sizeof(lone_high));
    expect_title_failure(lone_low, sizeof(lone_low));
    expect_title_failure(high_at_end, sizeof(high_at_end));
    expect_title_failure(odd_length, sizeof(odd_length));
    expect_title_failure(odd_be, sizeof(odd_be));
    expect_title_failure(bad_encoding, sizeof(bad_encoding));
}

/** @brief 控制字符整条取不到；首尾的控制类空白被裁掉。 */
static void test_control_characters(void)
{
    const uint8_t c0[] = {3, 'a', 0x01, 'b'};
    const uint8_t c1_utf8[] = {3, 'a', 0xC2, 0x85, 'b'};
    const uint8_t c1_latin1[] = {0, 'a', 0x85}; /* 末尾落单高位字节：不是 GBK，按 Latin-1 得到 C1 控制字符。 */
    const uint8_t interior_tab[] = {3, 'a', '\t', 'b'};
    const uint8_t trailing_crlf[] = {3, 'a', 'b', '\r', '\n'};
    const uint8_t only_control[] = {3, 0x07};
    const uint8_t on_artist[] = {3, 'x', 0x1F};
    StorageCatalog_TextTypeDef text;
    const uint8_t title[] = {3, 'T'};

    expect_title_failure(c0, sizeof(c0));
    expect_title_failure(c1_utf8, sizeof(c1_utf8));
    expect_title_failure(c1_latin1, sizeof(c1_latin1));
    expect_title_failure(interior_tab, sizeof(interior_tab));
    expect_title_failure(only_control, sizeof(only_control));
    expect_title(trailing_crlf, sizeof(trailing_crlf), "ab");

    file_reset();
    tag_begin(3U, 0U);
    frame(3U, "TIT2", 0U, title, sizeof(title));
    frame(3U, "TPE1", 0U, on_artist, sizeof(on_artist));
    tag_end(0U);
    audio(8U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_TAGGED, "T", "");
}

/** @brief 多值按 NUL 拆开、丢空项、以 " / " 连写；原文里的 '/' 保留；NUL 填充被去掉。 */
static void test_multi_value_and_padding(void)
{
    const uint8_t two[] = {3, 'A', 0, 'B'};
    const uint8_t three_with_empties[] = {3, 0, 'A', 0, 0, ' ', 0, 'B', 0, 'C', 0, 0};
    const uint8_t slash[] = {3, 'A', 'C', '/', 'D', 'C'};
    const uint8_t padded[] = {3, 'A', 0, 0, 0};
    const uint8_t padded_only[] = {3, 0, 0};

    expect_title(two, sizeof(two), "A / B");
    expect_title(three_with_empties, sizeof(three_with_empties), "A / B / C");
    expect_title(slash, sizeof(slash), "AC/DC");
    expect_title(padded, sizeof(padded), "A");
    expect_title_empty(padded_only, sizeof(padded_only));
}

/** @brief 每项去首尾空白（含 U+3000），内部空白保留；全空白按帧缺失回退。 */
static void test_whitespace_trimming(void)
{
    const uint8_t ascii[] = {3, ' ', ' ', 'a', ' ', 'b', ' ', ' '};
    const uint8_t ideographic[] = {3, 0xE3, 0x80, 0x80, 'a', 0xE3, 0x80, 0x80};
    const uint8_t per_value[] = {3, ' ', 'A', ' ', 0, ' ', 'B', ' '};
    const uint8_t blank[] = {3, ' ', 0xE3, 0x80, 0x80, '\t', ' '};
    const uint8_t blank_value_dropped[] = {3, 'A', 0, ' ', ' ', 0, 'B'};

    expect_title(ascii, sizeof(ascii), "a b");
    expect_title(ideographic, sizeof(ideographic), "a");
    expect_title(per_value, sizeof(per_value), "A / B");
    expect_title_empty(blank, sizeof(blank));
    expect_title(blank_value_dropped, sizeof(blank_value_dropped), "A / B");
}

/** @brief 超过 252 B 容量按字符边界截断、不加省略号，截断后仍算取到。 */
static void test_truncation(void)
{
    static uint8_t buf[2048];
    StorageCatalog_TextTypeDef text;
    uint32_t n;
    char expected[STORAGE_CATALOG_NAME_BYTES];

    buf[0] = 3U;
    n = 1U;
    for (uint32_t i = 0U; i < 300U; i++)
    {
        buf[n++] = 'a';
    }
    memset(expected, 'a', 251U);
    expected[251] = '\0';
    expect_title(buf, n, expected);

    /* 100 个三字节字符 = 300 B：最多放 83 个（249 B），不切半个字符。 */
    n = 1U;
    for (uint32_t i = 0U; i < 100U; i++)
    {
        memcpy(buf + n, "\xE6\x99\xB4", 3U);
        n += 3U;
    }
    run_title_payload(buf, n, &text);
    assert(text.Result == STORAGE_CATALOG_TEXT_TAGGED);
    assert(strlen(text.Title) == 249U);
    assert(memcmp(text.Title, "\xE6\x99\xB4\xE6\x99\xB4", 6U) == 0);

    /* 第二个值放不下时整项丢弃，不留下孤立的 " / "。 */
    n = 1U;
    for (uint32_t i = 0U; i < 250U; i++)
    {
        buf[n++] = 'a';
    }
    buf[n++] = 0U;
    buf[n++] = 'b';
    memset(expected, 'a', 250U);
    expected[250] = '\0';
    expect_title(buf, n, expected);

    /* 截断点之后的非法内容仍令整帧失败。 */
    n = 1U;
    for (uint32_t i = 0U; i < 300U; i++)
    {
        buf[n++] = 'a';
    }
    buf[n++] = 0xFFU;
    expect_title_failure(buf, n);
}

/** @brief 压缩或加密的帧、负载为空的帧按解码失败处理。 */
static void test_unreadable_frames(void)
{
    StorageCatalog_TextTypeDef text;
    const uint8_t plain[] = {3, 'T'};
    const uint8_t only_group[] = {3};

    file_reset();
    tag_begin(3U, 0U);
    frame(3U, "TIT2", 0x0080U, plain, sizeof(plain)); /* v2.3 压缩。 */
    tag_end(0U);
    audio(8U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_DECODE_FAILED, STEM, "");

    file_reset();
    tag_begin(3U, 0U);
    frame(3U, "TIT2", 0x0040U, plain, sizeof(plain)); /* v2.3 加密。 */
    tag_end(0U);
    audio(8U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_DECODE_FAILED, STEM, "");

    file_reset();
    tag_begin(4U, 0U);
    frame(4U, "TIT2", 0x0008U, plain, sizeof(plain)); /* v2.4 压缩。 */
    tag_end(0U);
    audio(8U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_DECODE_FAILED, STEM, "");

    file_reset();
    tag_begin(4U, 0U);
    frame(4U, "TIT2", 0x0004U, plain, sizeof(plain)); /* v2.4 加密。 */
    tag_end(0U);
    audio(8U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_DECODE_FAILED, STEM, "");

    file_reset();
    tag_begin(3U, 0U);
    frame(3U, "TIT2", 0U, plain, 0U); /* 负载为空：没有编码字节。 */
    tag_end(0U);
    audio(8U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_DECODE_FAILED, STEM, "");

    file_reset();
    tag_begin(4U, 0U);
    frame(4U, "TIT2", 0x0040U, only_group, 0U); /* 声明分组字节但负载为空。 */
    tag_end(0U);
    audio(8U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_DECODE_FAILED, STEM, "");
}

/** @brief v2.4 帧级去同步、数据长度指示与分组字节在帧完整位于块内时还原。 */
static void test_v24_frame_flags(void)
{
    StorageCatalog_TextTypeDef text;
    /* UTF-16LE "Hi"：编码 1，BOM FF FE。去同步后 FF 后插入 00。 */
    const uint8_t unsynced[] = {1, 0xFF, 0x00, 0xFE, 'H', 0, 'i', 0};
    const uint8_t with_dli[] = {0, 0, 0, 7, 3, 'O', 'K'};   /* 数据长度指示 syncsafe 7，其后 UTF-8 "OK"。 */
    const uint8_t with_group[] = {0x01, 3, 'G', 'P'};       /* 分组标识 1 字节，其后 UTF-8 "GP"。 */
    const uint8_t all_three[] = {0x05, 0, 0, 0, 9, 1, 0xFF, 0x00, 0xFE, 'Z', 0};

    file_reset();
    tag_begin(4U, 0U);
    frame(4U, "TIT2", 0x0002U, unsynced, sizeof(unsynced));
    tag_end(0U);
    audio(8U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_TAGGED, "Hi", "");

    file_reset();
    tag_begin(4U, 0U);
    frame(4U, "TIT2", 0x0001U, with_dli, sizeof(with_dli));
    tag_end(0U);
    audio(8U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_TAGGED, "OK", "");

    file_reset();
    tag_begin(4U, 0U);
    frame(4U, "TIT2", 0x0040U, with_group, sizeof(with_group));
    tag_end(0U);
    audio(8U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_TAGGED, "GP", "");

    file_reset();
    tag_begin(4U, 0U);
    frame(4U, "TIT2", 0x0043U, all_three, sizeof(all_three));
    tag_end(0U);
    audio(8U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_TAGGED, "Z", "");
}

/** @brief v2.3 的分组标志（0x20）跳过一个字节。 */
static void test_v23_grouping_flag(void)
{
    StorageCatalog_TextTypeDef text;
    const uint8_t with_group[] = {0x07, 3, 'G', 'P'};

    file_reset();
    tag_begin(3U, 0U);
    frame(3U, "TIT2", 0x0020U, with_group, sizeof(with_group));
    tag_end(0U);
    audio(8U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_TAGGED, "GP", "");
}

/** @brief 扩展头被跳过：v2.3 大小不含自身，v2.4 syncsafe 且含自身。 */
static void test_extended_header_skipped(void)
{
    StorageCatalog_TextTypeDef text;
    const uint8_t title[] = {3, 'E', 'X'};
    const uint8_t ext23[] = {0, 0, 0, 6, 0, 0, 0xAA, 0xAA, 0xAA, 0xAA};
    const uint8_t ext24[] = {0, 0, 0, 6, 1, 0};

    file_reset();
    tag_begin(3U, 0x40U);
    put(ext23, sizeof(ext23));
    frame(3U, "TIT2", 0U, title, sizeof(title));
    tag_end(0U);
    audio(8U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_TAGGED, "EX", "");

    file_reset();
    tag_begin(4U, 0x40U);
    put(ext24, sizeof(ext24));
    frame(4U, "TIT2", 0U, title, sizeof(title));
    tag_end(0U);
    audio(8U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_TAGGED, "EX", "");
}

/** @brief 帧顺序不固定；同种帧只认第一个；其他帧被跳过。 */
static void test_frame_order_and_duplicates(void)
{
    StorageCatalog_TextTypeDef text;
    const uint8_t album[] = {3, 'A', 'l', 'b'};
    const uint8_t first[] = {3, 'F'};
    const uint8_t second[] = {3, 'S'};
    const uint8_t artist[] = {3, 'P'};

    file_reset();
    tag_begin(3U, 0U);
    frame(3U, "TALB", 0U, album, sizeof(album));
    frame(3U, "TPE1", 0U, artist, sizeof(artist));
    frame(3U, "TIT2", 0U, first, sizeof(first));
    frame(3U, "TIT2", 0U, second, sizeof(second));
    tag_end(0U);
    audio(8U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_TAGGED, "F", "P");
}

/** @brief 无 ID3v2：没有标签头、文件过短、读到文件尾前不足标签头。 */
static void test_no_id3v2(void)
{
    StorageCatalog_TextTypeDef text;

    file_reset();
    audio(500U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_NO_ID3V2, STEM, "");
    assert(text.Reads == 1U);

    file_reset();
    audio(5U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_NO_ID3V2, STEM, "");
    assert(text.Reads == 0U);

    file_reset();
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_NO_ID3V2, STEM, "");
    assert(text.Reads == 0U);
}

/** @brief 帧缺失：只有无关帧、直接是填充、非法帧 ID、帧越过标签末尾、v2.4 非 syncsafe 帧大小。 */
static void test_frame_missing(void)
{
    StorageCatalog_TextTypeDef text;
    const uint8_t album[] = {3, 'A'};
    const uint8_t title[] = {3, 'T'};
    const uint8_t bad_syncsafe_header[10] = {'T', 'I', 'T', '2', 0x00, 0x00, 0x00, 0x81, 0, 0};

    file_reset();
    tag_begin(3U, 0U);
    frame(3U, "TALB", 0U, album, sizeof(album));
    tag_end(0U);
    audio(8U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_FRAME_MISSING, STEM, "");

    file_reset();
    tag_begin(3U, 0U);
    tag_end(200U);
    audio(8U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_FRAME_MISSING, STEM, "");

    /* 非法帧 ID（含填充）视为序列结束：其后的 TIT2 不再可见。 */
    file_reset();
    tag_begin(3U, 0U);
    tag_end(10U);
    frame(3U, "TIT2", 0U, title, sizeof(title));
    audio(8U);
    {
        uint32_t size = g_file.length - 10U - 8U;

        g_file.data[6] = (uint8_t)((size >> 21) & 0x7FU);
        g_file.data[7] = (uint8_t)((size >> 14) & 0x7FU);
        g_file.data[8] = (uint8_t)((size >> 7) & 0x7FU);
        g_file.data[9] = (uint8_t)(size & 0x7FU);
    }
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_FRAME_MISSING, STEM, "");

    /* 帧大小越过标签末尾：标签损坏，不读。 */
    file_reset();
    tag_begin(3U, 0U);
    frame(3U, "TIT2", 0U, title, sizeof(title));
    tag_end(0U);
    g_file.data[10 + 7] = 200U;
    audio(300U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_FRAME_MISSING, STEM, "");

    /* v2.4 帧大小字节最高位置位（非 syncsafe）：按非法帧结束。 */
    file_reset();
    tag_begin(4U, 0U);
    put(bad_syncsafe_header, sizeof(bad_syncsafe_header));
    put(title, sizeof(title));
    tag_end(0U);
    audio(8U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_FRAME_MISSING, STEM, "");
}

/** @brief 标签头不可读（版本不支持、大小字段非法、v2.2 压缩）按解码失败。 */
static void test_unreadable_tag_header(void)
{
    StorageCatalog_TextTypeDef text;

    file_reset();
    tag_begin(5U, 0U);
    tag_end(50U);
    audio(8U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_DECODE_FAILED, STEM, "");

    file_reset();
    tag_begin(3U, 0U);
    tag_end(50U);
    g_file.data[8] = 0x80U;
    audio(8U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_DECODE_FAILED, STEM, "");

    file_reset();
    tag_begin(2U, 0x40U);
    tag_end(50U);
    audio(8U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_DECODE_FAILED, STEM, "");
}

/** @brief TIT2 可用而 TPE1 坏：歌手为空，分类仍是取到；反之标题坏时歌手仍可用。 */
static void test_title_and_artist_independent(void)
{
    StorageCatalog_TextTypeDef text;
    const uint8_t good[] = {3, 'T'};
    const uint8_t bad[] = {4, 'x'};
    const uint8_t artist[] = {3, 'A'};

    file_reset();
    tag_begin(3U, 0U);
    frame(3U, "TIT2", 0U, good, sizeof(good));
    frame(3U, "TPE1", 0U, bad, sizeof(bad));
    tag_end(0U);
    audio(8U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_TAGGED, "T", "");

    file_reset();
    tag_begin(3U, 0U);
    frame(3U, "TIT2", 0U, bad, sizeof(bad));
    frame(3U, "TPE1", 0U, artist, sizeof(artist));
    tag_end(0U);
    audio(8U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_DECODE_FAILED, STEM, "A");

    file_reset();
    tag_begin(3U, 0U);
    frame(3U, "TPE1", 0U, artist, sizeof(artist));
    tag_end(0U);
    audio(8U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_FRAME_MISSING, STEM, "A");
}

/**
 * @brief 追加一个 APIC 样式的大帧（内容无关，只占位）。
 * @param[in] major 标签主版本。
 * @param[in] bytes 负载字节数。
 */
static void big_frame(uint8_t major, uint32_t bytes)
{
    static uint8_t filler[8192];

    assert(bytes <= sizeof(filler));
    memset(filler, 0x55, bytes);
    frame(major, "APIC", 0U, filler, bytes);
}

/** @brief 首块一次读完：偏移 0、请求一整块；文件短于一块时只读到文件尾。 */
static void test_first_block_read(void)
{
    StorageCatalog_TextTypeDef text;
    const uint8_t title[] = {3, 'T'};

    file_reset();
    tag_begin(3U, 0U);
    frame(3U, "TIT2", 0U, title, sizeof(title));
    tag_end(0U);
    audio(8000U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_TAGGED, "T", "");
    assert(text.Reads == 1U);
    assert(g_file.read_offset[0] == 0U);
    assert(g_file.read_bytes[0] == STORAGE_CATALOG_TEXT_BLOCK_BYTES);

    file_reset();
    tag_begin(3U, 0U);
    frame(3U, "TIT2", 0U, title, sizeof(title));
    tag_end(0U);
    audio(20U);
    run(&text);
    assert(text.Reads == 1U);
    assert(g_file.read_bytes[0] == g_file.length);
}

/** @brief APIC 前置且跨首块：seek 到 TIT2 帧头再读一块，不读 APIC 负载，不越过标签总长。 */
static void test_apic_first_seeks_past_cover(void)
{
    StorageCatalog_TextTypeDef text;
    const uint8_t title[] = {3, 'T'};
    const uint8_t artist[] = {3, 'A'};
    uint32_t title_header_offset;
    uint32_t tag_total;

    file_reset();
    tag_begin(3U, 0U);
    big_frame(3U, 6000U);
    title_header_offset = g_file.length;
    frame(3U, "TIT2", 0U, title, sizeof(title));
    frame(3U, "TPE1", 0U, artist, sizeof(artist));
    tag_total = g_file.length;
    tag_end(0U);
    audio(6000U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_TAGGED, "T", "A");
    assert(text.Reads == 2U);
    assert(g_file.read_offset[1] == title_header_offset);
    assert(g_file.read_bytes[1] == (tag_total - title_header_offset));
    assert(g_file.read_bytes[1] <= STORAGE_CATALOG_TEXT_BLOCK_BYTES);
}

/** @brief 目标帧头在块内但负载跨出块边界：从帧头重读一块。 */
static void test_frame_straddling_block_boundary(void)
{
    StorageCatalog_TextTypeDef text;
    uint8_t title[40];
    uint32_t header_offset;
    uint32_t n;

    file_reset();
    tag_begin(3U, 0U);
    big_frame(3U, STORAGE_CATALOG_TEXT_BLOCK_BYTES - 10U - 10U - 4U - 2U); /* 让 TIT2 帧头距块尾只剩 6 字节。 */
    header_offset = g_file.length;
    n = enc_text(title, 3U, BOM_NONE, false, "straddle");
    frame(3U, "TIT2", 0U, title, n);
    tag_end(0U);
    audio(100U);
    assert(header_offset == (STORAGE_CATALOG_TEXT_BLOCK_BYTES - 4U - 2U));
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_TAGGED, "straddle", "");
    assert(text.Reads == 2U);
    assert(g_file.read_offset[1] == header_offset);
}

/** @brief 含首块恰好 4 次读取可以取到；第 5 次需要时记超读取上限。 */
static void test_read_limit(void)
{
    StorageCatalog_TextTypeDef text;
    const uint8_t title[] = {3, 'T'};
    const uint8_t artist[] = {3, 'A'};

    file_reset();
    tag_begin(3U, 0U);
    big_frame(3U, 5000U);
    big_frame(3U, 5000U);
    big_frame(3U, 5000U);
    frame(3U, "TIT2", 0U, title, sizeof(title));
    tag_end(0U);
    audio(100U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_TAGGED, "T", "");
    assert(text.Reads == STORAGE_CATALOG_TEXT_MAX_READS);

    file_reset();
    tag_begin(3U, 0U);
    big_frame(3U, 5000U);
    big_frame(3U, 5000U);
    big_frame(3U, 5000U);
    big_frame(3U, 5000U);
    frame(3U, "TIT2", 0U, title, sizeof(title));
    tag_end(0U);
    audio(100U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_READ_LIMIT, STEM, "");
    assert(text.Reads == STORAGE_CATALOG_TEXT_MAX_READS);

    /* 标题已取到后歌手超限：分类仍是取到，歌手为空。 */
    file_reset();
    tag_begin(3U, 0U);
    frame(3U, "TIT2", 0U, title, sizeof(title));
    big_frame(3U, 5000U);
    big_frame(3U, 5000U);
    big_frame(3U, 5000U);
    big_frame(3U, 5000U);
    frame(3U, "TPE1", 0U, artist, sizeof(artist));
    tag_end(0U);
    audio(100U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_TAGGED, "T", "");
    assert(text.Reads == STORAGE_CATALOG_TEXT_MAX_READS);
}

/** @brief 目标帧负载大于一块：视为解码失败。 */
static void test_oversized_target_frame(void)
{
    static uint8_t huge[5000];
    StorageCatalog_TextTypeDef text;

    memset(huge, 'a', sizeof(huge));
    huge[0] = 3U;
    file_reset();
    tag_begin(3U, 0U);
    frame(3U, "TIT2", 0U, huge, sizeof(huge));
    tag_end(0U);
    audio(100U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_DECODE_FAILED, STEM, "");
}

/** @brief 读取错误注入：首块失败、seek 后失败；标题已取到后的失败不改变分类。 */
static void test_read_errors(void)
{
    StorageCatalog_TextTypeDef text;
    const uint8_t title[] = {3, 'T'};
    const uint8_t artist[] = {3, 'A'};

    file_reset();
    tag_begin(3U, 0U);
    frame(3U, "TIT2", 0U, title, sizeof(title));
    tag_end(0U);
    audio(100U);
    g_file.fail_at = 1U;
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_READ_ERROR, STEM, "");
    assert(text.Reads == 1U);

    file_reset();
    tag_begin(3U, 0U);
    big_frame(3U, 6000U);
    frame(3U, "TIT2", 0U, title, sizeof(title));
    tag_end(0U);
    audio(100U);
    g_file.fail_at = 2U;
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_READ_ERROR, STEM, "");
    assert(text.Reads == 2U);

    file_reset();
    tag_begin(3U, 0U);
    frame(3U, "TIT2", 0U, title, sizeof(title));
    big_frame(3U, 6000U);
    frame(3U, "TPE1", 0U, artist, sizeof(artist));
    tag_end(0U);
    audio(100U);
    g_file.fail_at = 2U;
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_TAGGED, "T", "");
    assert(text.Reads == 2U);
}

/**
 * @brief 把当前假文件的标签体做标签级去同步：每个 0xFF 后插入 0x00，并回填标签大小。
 * @note  帧大小保持去同步前的值，与真实 v2.3 标签一致。
 */
static void unsync_whole_tag(void)
{
    static uint8_t tmp[FAKE_FILE_MAX];
    uint32_t n = 0U;
    uint32_t size;

    for (uint32_t i = 10U; i < g_file.length; i++)
    {
        tmp[n++] = g_file.data[i];
        if (g_file.data[i] == 0xFFU)
        {
            tmp[n++] = 0U;
        }
    }
    memcpy(g_file.data + 10, tmp, n);
    g_file.length = 10U + n;
    size = n;
    g_file.data[6] = (uint8_t)((size >> 21) & 0x7FU);
    g_file.data[7] = (uint8_t)((size >> 14) & 0x7FU);
    g_file.data[8] = (uint8_t)((size >> 7) & 0x7FU);
    g_file.data[9] = (uint8_t)(size & 0x7FU);
}

/** @brief 标签级去同步的 v2.3：首块内还原后正常解析；标签超出首块时只解析首块。 */
static void test_v23_tag_level_unsync(void)
{
    StorageCatalog_TextTypeDef text;
    const uint8_t title[] = {1, 0xFF, 0xFE, 'H', 0, 'i', 0};
    const uint8_t artist[] = {1, 0xFF, 0xFE, 'B', 0};

    file_reset();
    tag_begin(3U, 0x80U);
    frame(3U, "TIT2", 0U, title, sizeof(title));
    frame(3U, "TPE1", 0U, artist, sizeof(artist));
    tag_end(0U);
    unsync_whole_tag();
    audio(100U);
    assert(memchr(g_file.data + 10, 0xFF, 40U) != NULL);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_TAGGED, "Hi", "B");
    assert(text.Reads == 1U);

    /* 标题在首块之外：不 seek，记超读取上限。 */
    file_reset();
    tag_begin(3U, 0x80U);
    big_frame(3U, 6000U);
    frame(3U, "TIT2", 0U, title, sizeof(title));
    tag_end(0U);
    unsync_whole_tag();
    audio(100U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_READ_LIMIT, STEM, "");
    assert(text.Reads == 1U);

    /* 标签在首块内且没有目标帧：正常结束为帧缺失。 */
    file_reset();
    tag_begin(3U, 0x80U);
    big_frame(3U, 100U);
    tag_end(0U);
    unsync_whole_tag();
    audio(100U);
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_FRAME_MISSING, STEM, "");
}

/** @brief 标签大于文件长度时夹到文件尾，不越界。 */
static void test_tag_size_beyond_file(void)
{
    StorageCatalog_TextTypeDef text;
    const uint8_t title[] = {3, 'T'};

    file_reset();
    tag_begin(3U, 0U);
    frame(3U, "TIT2", 0U, title, sizeof(title));
    tag_end(0U);
    g_file.data[8] = 0x7FU; /* 声明的标签远大于文件。 */
    run(&text);
    expect(&text, STORAGE_CATALOG_TEXT_TAGGED, "T", "");
    assert(text.Reads == 1U);
}

/** @brief 文件主名提取失败是防御分支：函数返回 false，标题与歌手为空串。 */
static void test_stem_failure_is_defensive_branch(void)
{
    static const char path[] = "Music/";
    StorageCatalog_TextTypeDef text;
    StorageCatalog_TextRequestTypeDef request = {
        path, (uint32_t)(sizeof(path) - 1U), 0U, fake_read, &g_file, gbk_lookup, NULL, g_block};

    file_reset();
    memset(&text, 0x5A, sizeof(text));
    assert(!storage_catalog_track_text(&request, &text));
    assert(text.Title[0] == '\0');
    assert(text.Artist[0] == '\0');
    assert(text.Reads == 0U);
}

/**
 * @brief 测试入口。
 * @return 0 表示全部通过（断言失败则终止）。
 */
int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    RUN_TEST(test_v23_utf16le_title_and_artist);
    RUN_TEST(test_v22_frames);
    RUN_TEST(test_v24_utf8);
    RUN_TEST(test_encoding_zero_latin1_and_gbk);
    RUN_TEST(test_gbk_needs_lookup);
    RUN_TEST(test_utf16_variants);
    RUN_TEST(test_utf16_bom_per_value);
    RUN_TEST(test_invalid_text_fails_whole_frame);
    RUN_TEST(test_control_characters);
    RUN_TEST(test_multi_value_and_padding);
    RUN_TEST(test_whitespace_trimming);
    RUN_TEST(test_truncation);
    RUN_TEST(test_unreadable_frames);
    RUN_TEST(test_v24_frame_flags);
    RUN_TEST(test_v23_grouping_flag);
    RUN_TEST(test_extended_header_skipped);
    RUN_TEST(test_frame_order_and_duplicates);
    RUN_TEST(test_no_id3v2);
    RUN_TEST(test_frame_missing);
    RUN_TEST(test_unreadable_tag_header);
    RUN_TEST(test_title_and_artist_independent);
    RUN_TEST(test_first_block_read);
    RUN_TEST(test_apic_first_seeks_past_cover);
    RUN_TEST(test_frame_straddling_block_boundary);
    RUN_TEST(test_read_limit);
    RUN_TEST(test_oversized_target_frame);
    RUN_TEST(test_read_errors);
    RUN_TEST(test_v23_tag_level_unsync);
    RUN_TEST(test_tag_size_beyond_file);
    RUN_TEST(test_stem_failure_is_defensive_branch);
    puts("storage_catalog_text: all tests passed");
    return 0;
}
