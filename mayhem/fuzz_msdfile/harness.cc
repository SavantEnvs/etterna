// Fuzzes Etterna's .sm/.ssc/.dwi tag tokenizer (MsdFile).
#include "Etterna/Globals/global.h"
#include "Etterna/FileTypes/MsdFile.h"

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	if (size < 1)
		return -1;

	const bool unescape = (data[0] & 1) != 0;
	const std::string text(reinterpret_cast<const char*>(data + 1), size - 1);

	MsdFile msd;
	msd.ReadFromString(text, unescape);
	for (unsigned v = 0; v < msd.GetNumValues(); ++v)
		for (unsigned p = 0; p < msd.GetNumParams(v); ++p)
			(void)msd.GetParam(v, p).size();
	return 0;
}
