// Behavioral known-answer checks for the parsing/calculation code that mayhem/build.sh builds
// standalone. Prints one "PASS <name>" / "FAIL <name>" line per check; mayhem/test.sh counts them.
#include "Etterna/Globals/global.h"
#include "Etterna/FileTypes/MsdFile.h"
#include "Etterna/MinaCalc/MinaCalc.h"
#include "RageUtil/Misc/RageUnicode.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static constexpr float SLOW_STREAM_MSD = 7.182F;
static constexpr float FAST_STREAM_MSD = 9.867F;

static void
check(const char* name, bool ok)
{
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
}

static MsdFile
parse(const std::string& text, bool unescape = false)
{
	MsdFile msd;
	msd.ReadFromString(text, unescape);
	return msd;
}

static void
test_msd()
{
	auto m = parse("#TITLE:Hello World;#ARTIST:Me;");
	check("msd_two_values", m.GetNumValues() == 2);
	check("msd_title_key", m.GetParam(0, 0) == "TITLE");
	check("msd_title_value", m.GetParam(0, 1) == "Hello World");
	check("msd_artist_value", m.GetNumParams(1) == 2 && m.GetParam(1, 1) == "Me");

	m = parse("#BPMS:0.000=120.000,16.000=240.000;");
	check("msd_bpms_one_param", m.GetNumParams(0) == 2 &&
								  m.GetParam(0, 1) == "0.000=120.000,16.000=240.000");

	m = parse("#NOTES:dance-single:desc:Hard:10:0,0,0,0,0:0000\n0000\n;");
	check("msd_notes_seven_params", m.GetNumParams(0) == 7);
	check("msd_notes_meter", m.GetParam(0, 4) == "10");
	check("msd_notes_body", m.GetParam(0, 6) == "0000\n0000\n");

	m = parse("// header comment\n#X:1;");
	check("msd_comment_skipped", m.GetNumValues() == 1 && m.GetParam(0, 0) == "X");

	m = parse("#A:x\\:y;", true);
	check("msd_unescape_colon", m.GetNumParams(0) == 2 && m.GetParam(0, 1) == "x:y");
	m = parse("#A:x\\:y;", false);
	check("msd_no_unescape_splits", m.GetNumParams(0) == 3 && m.GetParam(0, 2) == "y");

	m = parse("#A:1\n#B:2\n");
	check("msd_missing_semicolon_recovers", m.GetNumValues() == 2 &&
											  m.GetParam(0, 1) == "1" &&
											  m.GetParam(1, 1) == "2\n");

	m = parse("");
	check("msd_empty", m.GetNumValues() == 0 && m.GetParam(3, 3).empty());
}

static void
test_utf8()
{
	check("utf8_len_ascii", Rage::utf8_get_char_len('a') == 1);
	check("utf8_len_two", Rage::utf8_get_char_len(static_cast<char>(0xC3)) == 2);
	check("utf8_len_three", Rage::utf8_get_char_len(static_cast<char>(0xE2)) == 3);
	check("utf8_len_four", Rage::utf8_get_char_len(static_cast<char>(0xF0)) == 4);

	std::string out;
	Rage::wchar_to_utf8(0x20AC, out);
	check("utf8_encode_euro", out == "\xE2\x82\xAC");
	out.clear();
	Rage::wchar_to_utf8(0xE9, out);
	check("utf8_encode_eacute", out == "\xC3\xA9");

	const std::string eacute = "\xC3\xA9";
	unsigned start = 0;
	wchar_t ch = 0;
	check("utf8_decode_eacute",
		  Rage::utf8_to_wchar_ec(eacute, start, ch) && ch == 0xE9 && start == 2);
	check("utf8_get_char", Rage::utf8_get_char("\xE2\x82\xAC") == 0x20AC);

	check("utf8_valid_text", Rage::utf8_is_valid("h\xC3\xA9llo"));
	check("utf8_invalid_bytes", !Rage::utf8_is_valid("\xFF\xFE"));
	std::string bom = "\xEF\xBB\xBF" "abc";
	Rage::utf8_remove_bom(bom);
	check("utf8_remove_bom", bom == "abc");

	check("utf8_upper", Rage::make_upper(std::wstring(L"abc")) == L"ABC");
	check("utf8_lower", Rage::make_lower(std::wstring(L"XyZ")) == L"xyz");
}

static std::vector<NoteInfo>
stream_chart(unsigned keycount, int rows, float step)
{
	std::vector<NoteInfo> v;
	for (int i = 0; i < rows; ++i)
		v.push_back(NoteInfo{ 1U << (i % keycount), step * static_cast<float>(i + 1) });
	return v;
}

static float
overall(const std::vector<NoteInfo>& chart, float rate, unsigned keycount)
{
	Calc calc;
	return MinaSDCalc(chart, rate, 0.93F, keycount, &calc).at(Skill_Overall);
}

static void
test_calc()
{
	Calc calc;
	auto empty = MinaSDCalc({}, 1.F, 0.93F, 4, &calc);
	bool all_zero = !empty.empty();
	for (float f : empty)
		all_zero = all_zero && f == 0.F;
	check("calc_empty_chart_is_zero", all_zero);
	check("calc_version", GetCalcVersion() == 527);

	const auto slow = stream_chart(4, 1200, 0.125F);
	const auto fast = stream_chart(4, 1200, 0.0625F);
	const float s = overall(slow, 1.F, 4);
	const float f = overall(fast, 1.F, 4);
	const float f_fast_rate = overall(slow, 2.F, 4);
	check("calc_stream_finite_positive", std::isfinite(s) && s > 1.F && s < 40.F);
	check("calc_faster_is_harder", f > s + 2.F);
	check("calc_rate_scales_difficulty", f_fast_rate > s + 2.F);
	check("calc_rate_matches_denser_chart", std::fabs(f_fast_rate - f) < 1.5F);
	check("calc_stream_known_value", std::fabs(s - SLOW_STREAM_MSD) < 0.05F);
	check("calc_dense_known_value", std::fabs(f - FAST_STREAM_MSD) < 0.05F);

	const auto six = stream_chart(6, 1200, 0.125F);
	check("calc_six_key_positive", overall(six, 1.F, 6) > 1.F);
}

int
main()
{
	test_msd();
	test_utf8();
	test_calc();
	return 0;
}
