/* made by tools/mkmusic.py - MUSIC.XA layout: channel, first row (8 sectors a row), rows */
#define MUSIC_TRACKS 16
#define MUSIC_TITLE 0
static const struct { uint8_t chan; uint16_t row, rows; } music_tracks[MUSIC_TRACKS] = {
	{ 4, 0, 3296 },   /* revenge, 176 s */
	{ 5, 0, 3241 },   /* 02_fanatic-conquerer, 173 s */
	{ 3, 0, 3408 },   /* 01_fanatic-assault, 182 s */
	{ 2, 0, 3751 },   /* 05_fanatic-lost-in-the-mist, 200 s */
	{ 7, 3020, 2870 },   /* 03_fanatic-tranquility, 153 s */
	{ 6, 0, 3042 },   /* 04_fanatic-unreleased-rage, 162 s */
	{ 4, 3315, 2100 },   /* 08_fanatic-waiting-to-strike, 112 s */
	{ 5, 3260, 2250 },   /* 09_fanatic-disrupting-force, 120 s */
	{ 1, 4627, 1531 },   /* 06_fanatic-hit-them-hard, 82 s */
	{ 4, 5434, 746 },   /* ambient, 40 s */
	{ 3, 3427, 2000 },   /* 10_fanatic-my-destiny, 107 s */
	{ 2, 3770, 1800 },   /* 07_fanatic-cubed-to-death, 96 s */
	{ 6, 3061, 2632 },   /* deathB, 140 s */
	{ 1, 0, 4608 },   /* cube1, 246 s */
	{ 0, 0, 5625 },   /* cubez0r, 300 s */
	{ 7, 0, 3001 },   /* 11_fanatic-set-free, 160 s */
};
/* catalog index -> track */
static const uint8_t music_of_cat[15] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
