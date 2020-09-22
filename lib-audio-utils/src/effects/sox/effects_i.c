/* Implements a libSoX internal interface for implementing effects.
 * All public functions & data are prefixed with lsx_ .
 *
 * Copyright (c) 2005-2012 Chris Bagwell and SoX contributors
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#define LSX_EFF_ALIAS

#include "sox.h"
#include <ctype.h>

char * lsx_usage_lines(char * * usage, char const * const * lines, size_t n)
{
  if (!*usage) {
    size_t i, len;
    for (len = i = 0; i < n; len += strlen(lines[i++]) + 1);
    *usage = malloc(len); /* FIXME: this memory will never be freed */
    strcpy(*usage, lines[0]);
    for (i = 1; i < n; ++i) {
      strcat(*usage, "\n");
      strcat(*usage, lines[i]);
    }
  }
  return *usage;
}

/* a note is given as an int,
 * 0   => 440 Hz = A
 * >0  => number of half notes 'up',
 * <0  => number of half notes down,
 * example 12 => A of next octave, 880Hz
 *
 * calculated by freq = 440Hz * 2**(note/12)
 */
static double calc_note_freq(double note, int key)
{
  if (key != INT_MAX) {                         /* Just intonation. */
    static const int n[] = {16, 9, 6, 5, 4, 7}; /* Numerator. */
    static const int d[] = {15, 8, 5, 4, 3, 5}; /* Denominator. */
    static double j[13];                        /* Just semitones */
    int i, m = floor(note);

    if (!j[1]) for (i = 1; i <= 12; ++i)
      j[i] = i <= 6? log((double)n[i - 1] / d[i - 1]) / log(2.) : 1 - j[12 - i];
    note -= m;
    m -= key = m - ((INT_MAX / 2 - ((INT_MAX / 2) % 12) + m - key) % 12);
    return 440 * pow(2., key / 12. + j[m] + (j[m + 1] - j[m]) * note);
  }
  return 440 * pow(2., note / 12);
}

int lsx_parse_note(char const * text, char * * end_ptr)
{
  int result = INT_MAX;

  if (*text >= 'A' && *text <= 'G') {
    result = (int)(5/3. * (*text++ - 'A') + 9.5) % 12 - 9;
    if (*text == 'b') {--result; ++text;}
    else if (*text == '#') {++result; ++text;}
    if (isdigit((unsigned char)*text))
      result += 12 * (*text++ - '4');
  }
  *end_ptr = (char *)text;
  return result;
}

/* Read string 'text' and convert to frequency.
 * 'text' can be a positive number which is the frequency in Hz.
 * If 'text' starts with a '%' and a following number the corresponding
 * note is calculated.
 * Return -1 on error.
 */
double lsx_parse_frequency_k(char const * text, char * * end_ptr, int key)
{
  double result;

  if (*text == '%') {
    result = strtod(text + 1, end_ptr);
    if (*end_ptr == text + 1)
      return -1;
    return calc_note_freq(result, key);
  }
  if (*text >= 'A' && *text <= 'G') {
    int result2 = lsx_parse_note(text, end_ptr);
    return result2 == INT_MAX? - 1 : calc_note_freq((double)result2, key);
  }
  result = strtod(text, end_ptr);
  if (end_ptr) {
    if (*end_ptr == text)
      return -1;
    if (**end_ptr == 'k') {
      result *= 1000;
      ++*end_ptr;
    }
  }
  return result < 0 ? -1 : result;
}
