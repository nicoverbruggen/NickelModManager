# Embed bytes rather than a raw string, so quotes and delimiters need no escaping.
file(READ "${INPUT}" bytes HEX)
string(REGEX REPLACE "(..)" "\\\\x\\1" literal "${bytes}")
file(WRITE "${OUTPUT}" "#pragma once\nconstexpr char ${SYMBOL}[] = \"${literal}\";\n")
