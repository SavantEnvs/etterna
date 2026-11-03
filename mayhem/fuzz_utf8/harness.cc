// Fuzzes Etterna's UTF-8 helpers (RageUnicode): decode, re-encode, case-fold.
#include "RageUtil/Misc/RageUnicode.h"

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	std::string s(reinterpret_cast<const char*>(data), size);

	(void)Rage::utf8_is_valid(s);

	std::wstring wide;
	for (unsigned start = 0; start < s.size();) {
		wchar_t ch = 0;
		if (!Rage::utf8_to_wchar(s.data(), s.size(), start, ch))
			break;
		wide.push_back(ch);
	}
	(void)Rage::make_lower(wide);
	(void)Rage::make_upper(wide);

	std::string out;
	for (wchar_t ch : wide)
		Rage::wchar_to_utf8(ch, out);
	if (!s.empty())
		(void)Rage::utf8_get_char(s);

	unsigned start = 0;
	wchar_t ch = 0;
	while (start < s.size() && Rage::utf8_to_wchar_ec(s, start, ch)) {
	}

	Rage::utf8_remove_bom(s);
	return 0;
}
