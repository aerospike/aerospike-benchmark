/*******************************************************************************
 * Copyright 2008-2018 by Aerospike.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 ******************************************************************************/

#include <string.h>
#include <time.h>

#include "common.h"

void
blog_line(const char* fmt, ...)
{
	char fmtbuf[1024];
	size_t len = strlen(fmt);
	memcpy(fmtbuf, fmt, len);
	char* p = fmtbuf + len;
	*p++ = '\n';
	*p = 0;
	
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmtbuf, ap);
	va_end(ap);
}

void
blog_detailv(as_log_level level, const char* fmt, va_list ap)
{
	// Write message all at once so messages generated from multiple threads have less of a chance
	// of getting garbled.
	char fmtbuf[1024];
	time_t now = time(NULL);
	struct tm* t = localtime(&now);
	int len = sprintf(fmtbuf, "%d-%02d-%02d %02d:%02d:%02d %s ",
		t->tm_year+1900, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec, as_log_level_tostring(level));
	size_t len2 = strlen(fmt);
	char* p = fmtbuf + len;
	memcpy(p, fmt, len2);
	p += len2;
	*p++ = '\n';
	*p = 0;
	
	vprintf(fmtbuf, ap);
}

void
blog_detail(as_log_level level, const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	blog_detailv(level, fmt, ap);
	va_end(ap);
}
