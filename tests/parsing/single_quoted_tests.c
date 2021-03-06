#include <stic.h>

#include <stdlib.h> /* free() */

#include "../../src/engine/parsing.h"
#include "../../src/engine/var.h"

#include "asserts.h"

TEST(empty_ok)
{
	ASSERT_OK("''", "");
}

TEST(simple_ok)
{
	ASSERT_OK("'test'", "test");
}

TEST(not_closed_error)
{
	ASSERT_FAIL("'test", PE_MISSING_QUOTE);
}

TEST(concatenation)
{
	ASSERT_OK("'NV'.'AR'", "NVAR");
	ASSERT_OK("'NV' .'AR'", "NVAR");
	ASSERT_OK("'NV'. 'AR'", "NVAR");
	ASSERT_OK("'NV' . 'AR'", "NVAR");
}

TEST(double_single_quote_ok)
{
	ASSERT_OK("''''", "'");
	ASSERT_OK("'foo''bar'", "foo'bar");
}

TEST(triple_single_quote_error)
{
	ASSERT_FAIL("'''''", PE_MISSING_QUOTE);
}

TEST(single_slash_ok)
{
	ASSERT_OK("'\\'", "\\");
}

TEST(slash_t_ok)
{
	ASSERT_OK("'\\t'", "\\t");
}

TEST(spaces_ok)
{
	ASSERT_OK("' s y '", " s y ");
}

TEST(dot_ok)
{
	ASSERT_OK("'a . c'", "a . c");
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 : */
