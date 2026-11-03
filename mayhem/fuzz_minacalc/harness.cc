// Fuzzes Etterna's difficulty calculator (MinaCalc) with a synthetic chart.
// Input layout: [keycount sel][rate sel][goal sel][flags] then 3-byte rows
// (column mask, 16-bit time delta in ms).
#include "Etterna/MinaCalc/MinaCalc.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	if (size < 4 + 3 * 2)
		return -1;

	const unsigned keycount = 4 + (data[0] % 4);
	const float rate = 0.5F + static_cast<float>(data[1] % 31) / 10.F;
	const float goal = 0.80F + static_cast<float>(data[2] % 20) / 100.F;
	const bool debug = (data[3] & 1) != 0;
	const unsigned mask = (1U << keycount) - 1U;

	std::vector<NoteInfo> rows;
	float t = 0.F;
	for (size_t i = 4; i + 3 <= size; i += 3) {
		const unsigned notes = data[i] & mask;
		if (notes == 0)
			continue;
		const unsigned delta = 1 + (data[i + 1] | (data[i + 2] << 8));
		t += static_cast<float>(delta) / 1000.F;
		rows.push_back(NoteInfo{ notes, t });
	}
	if (rows.size() < 2)
		return -1;

	Calc calc;
	if (debug) {
		std::vector<std::vector<std::vector<std::vector<float>>>> hand_info;
		std::vector<std::string> strings;
		MinaSDCalcDebug(rows, rate, goal, keycount, hand_info, strings, calc);
	} else {
		(void)MinaSDCalc(rows, rate, goal, keycount, &calc);
	}
	return 0;
}
