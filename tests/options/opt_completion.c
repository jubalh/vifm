#include <stic.h>

#include <stdlib.h>

#include "../../src/engine/completion.h"
#include "../../src/engine/options.h"

TEST(space_at_the_end)
{
	const char *start;
	char *completed;

	vle_compl_reset();
	complete_options("fusehome=a\\ b\\ ", &start);
	completed = vle_compl_next();
	assert_string_equal("a\\ b\\ ", completed);
	free(completed);

	vle_compl_reset();
	complete_options("fusehome=a\\ b ", &start);

	completed = vle_compl_next();
	assert_string_equal("all", completed);
	free(completed);

	completed = vle_compl_next();
	assert_string_equal("cdpath", completed);
	free(completed);

	completed = vle_compl_next();
	assert_string_equal("cpoptions", completed);
	free(completed);

	completed = vle_compl_next();
	assert_string_equal("fastrun", completed);
	free(completed);
}

TEST(one_choice_opt)
{
	const char *start;
	char *completed;

	vle_compl_reset();
	complete_options("fuse", &start);

	completed = vle_compl_next();
	assert_string_equal("fusehome", completed);
	free(completed);

	completed = vle_compl_next();
	assert_string_equal("fusehome", completed);
	free(completed);

	completed = vle_compl_next();
	assert_string_equal("fusehome", completed);
	free(completed);
}

TEST(one_choice_val)
{
	char buf[] = "sort=n";
	const char *start;
	char *completed;

	vle_compl_reset();
	complete_options(buf, &start);
	assert_true(start == buf + 5);

	completed = vle_compl_next();
	assert_string_equal("name", completed);
	free(completed);

	completed = vle_compl_next();
	assert_string_equal("name", completed);
	free(completed);

	completed = vle_compl_next();
	assert_string_equal("name", completed);
	free(completed);
}

TEST(invalid_input)
{
	const char *start;
	char *completed;

	vle_compl_reset();
	complete_options("fast ?f", &start);
	completed = vle_compl_next();
	assert_string_equal("fast ?f", completed);
	free(completed);
}

TEST(skip_abbreviations)
{
	const char *start;
	char *completed;

	vle_compl_reset();
	complete_options("", &start);

	completed = vle_compl_next();
	assert_string_equal("all", completed);
	free(completed);

	completed = vle_compl_next();
	assert_string_equal("cdpath", completed);
	free(completed);

	completed = vle_compl_next();
	assert_string_equal("cpoptions", completed);
	free(completed);

	completed = vle_compl_next();
	assert_string_equal("fastrun", completed);
	free(completed);

	completed = vle_compl_next();
	assert_string_equal("fusehome", completed);
	free(completed);

	completed = vle_compl_next();
	assert_string_equal("sort", completed);
	free(completed);

	completed = vle_compl_next();
	assert_string_equal("sortorder", completed);
	free(completed);

	completed = vle_compl_next();
	assert_string_equal("tabstop", completed);
	free(completed);

	completed = vle_compl_next();
	assert_string_equal("vifminfo", completed);
	free(completed);

	completed = vle_compl_next();
	assert_string_equal("", completed);
	free(completed);
}

TEST(expand_abbreviations)
{
	const char *start;
	char *completed;

	vle_compl_reset();
	complete_options("fr", &start);

	completed = vle_compl_next();
	assert_string_equal("fastrun", completed);
	free(completed);

	completed = vle_compl_next();
	assert_string_equal("fastrun", completed);
	free(completed);
}

TEST(after_eq_value_completion)
{
	char buf[] = "vifminfo=op";
	const char *start;
	char *completed;

	vle_compl_reset();
	complete_options(buf, &start);
	assert_true(start == buf + 9);

	completed = vle_compl_next();
	assert_string_equal("options", completed);
	free(completed);

	completed = vle_compl_next();
	assert_string_equal("options", completed);
	free(completed);
}

TEST(after_meq_value_completion)
{
	char buf[] = "vifminfo-=op";
	const char *start;
	char *completed;

	vle_compl_reset();
	complete_options(buf, &start);
	assert_true(start == buf + 10);

	completed = vle_compl_next();
	assert_string_equal("options", completed);
	free(completed);

	completed = vle_compl_next();
	assert_string_equal("options", completed);
	free(completed);
}

TEST(after_peq_value_completion)
{
	char buf[] = "vifminfo+=op";
	const char *start;
	char *completed;

	vle_compl_reset();
	complete_options(buf, &start);
	assert_true(start == buf + 10);

	completed = vle_compl_next();
	assert_string_equal("options", completed);
	free(completed);

	completed = vle_compl_next();
	assert_string_equal("options", completed);
	free(completed);
}

TEST(set_values_completion)
{
	char buf[] = "vifminfo=tui,c";
	const char *start;
	char *completed;

	vle_compl_reset();
	complete_options(buf, &start);
	assert_true(start == buf + 13);

	completed = vle_compl_next();
	assert_string_equal("commands", completed);
	free(completed);

	completed = vle_compl_next();
	assert_string_equal("cs", completed);
	free(completed);

	completed = vle_compl_next();
	assert_string_equal("c", completed);
	free(completed);
}

TEST(colon)
{
	const char *start;
	char *completed;

	vle_compl_reset();
	complete_options("fusehome:a\\ b\\ ", &start);
	completed = vle_compl_next();
	assert_string_equal("a\\ b\\ ", completed);
	free(completed);

	vle_compl_reset();
	complete_options("fusehome:a\\ b ", &start);

	completed = vle_compl_next();
	assert_string_equal("all", completed);
	free(completed);

	completed = vle_compl_next();
	assert_string_equal("cdpath", completed);
	free(completed);

	completed = vle_compl_next();
	assert_string_equal("cpoptions", completed);
	free(completed);

	completed = vle_compl_next();
	assert_string_equal("fastrun", completed);
	free(completed);
}

TEST(umbiguous_beginning)
{
	const char *start;
	char *completed;

	vle_compl_reset();
	complete_options("sort", &start);

	completed = vle_compl_next();
	assert_string_equal("sort", completed);
	free(completed);

	completed = vle_compl_next();
	assert_string_equal("sortorder", completed);
	free(completed);
}

TEST(matching_short_full)
{
	const char *start;
	char *completed;

	vle_compl_reset();
	complete_options("so", &start);

	completed = vle_compl_next();
	assert_string_equal("sort", completed);
	free(completed);

	completed = vle_compl_next();
	assert_string_equal("sortorder", completed);
	free(completed);

	completed = vle_compl_next();
	assert_string_equal("so", completed);
	free(completed);
}

TEST(after_equal_sign_completion_ok)
{
	const char *start;
	char *completed;

	optval_t val = { .str_val = "/home/tmp" };
	set_option("fusehome", val);

	vle_compl_reset();
	complete_options("fusehome=", &start);

	completed = vle_compl_next();
	assert_string_equal("/home/tmp", completed);
	free(completed);
}

TEST(after_equal_sign_completion_spaces_ok)
{
	const char *start;
	char *completed;

	optval_t val = { .str_val = "/home directory/tmp" };
	set_option("fusehome", val);

	vle_compl_reset();
	complete_options("fusehome=", &start);

	completed = vle_compl_next();
	assert_string_equal("/home\\ directory/tmp", completed);
	free(completed);
}

TEST(after_fake_equal_sign_completion_fail)
{
	const char *start;
	char *completed;

	optval_t val = { .str_val = "/home/tmp" };
	set_option("fusehome", val);

	vle_compl_reset();
	complete_options("fusehome=a=", &start);

	completed = vle_compl_next();
	assert_string_equal("a=", completed);
	free(completed);
}

TEST(all_completion_ok)
{
	const char *start;
	char *completed;

	vle_compl_reset();
	complete_options("all=", &start);

	completed = vle_compl_next();
	assert_string_equal("", completed);
	free(completed);
}

TEST(charset_completion_skips_entered_elements)
{
	const char *start;
	char *completed;

	vle_compl_reset();
	complete_options("cpo=a", &start);

	completed = vle_compl_next();
	assert_string_equal("b", completed);
	free(completed);

	completed = vle_compl_next();
	assert_string_equal("c", completed);
	free(completed);

	completed = vle_compl_next();
	assert_string_equal("", completed);
	free(completed);

	completed = vle_compl_next();
	assert_string_equal("b", completed);
	free(completed);
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 : */
