# 把一个二进制文件原样生成为 C 字节数组（构建期运行，输出在构建目录，不入库）。
#
# 用法：cmake -DINPUT=<bin> -DOUTPUT=<c> -DSYMBOL=<C 标识符> -P embed_bin.cmake
# 生成 `const uint8_t <SYMBOL>[]` 与 `const uint32_t <SYMBOL>_size`，字节与输入逐一相同。

if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT OR NOT DEFINED SYMBOL)
    message(FATAL_ERROR "embed_bin.cmake 需要 INPUT、OUTPUT 与 SYMBOL")
endif()

file(READ "${INPUT}" content HEX)
string(LENGTH "${content}" hex_length)
math(EXPR byte_count "${hex_length} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," body "${content}")

file(WRITE "${OUTPUT}"
    "/* 由 Tools/gui_simulator/cmake/embed_bin.cmake 从 ${INPUT} 生成，勿手改。 */\n"
    "#include <stdint.h>\n"
    "const uint32_t ${SYMBOL}_size = ${byte_count}U;\n"
    "const uint8_t ${SYMBOL}[] = {${body}};\n")
