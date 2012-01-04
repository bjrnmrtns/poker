#include <cstdio>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string>
#include <stdint.h>
#include <time.h>

#pragma pack (push, 1)
struct PokerGame
{
	struct Card
	{
		uint8_t rank;
		uint8_t suit;
		uint8_t padding;
	};
	struct PlayerHand 
	{
		Card card[5];
	};
	PlayerHand playerHand[2];
	uint8_t padding_lf;
};
#pragma pack(pop)

class CardBitFields
{
public:
	uint64_t Count['Z'];
	CardBitFields()
	{
		Count['2'] = 1 << 4;
		Count['3'] = Count['2'] << 4;
		Count['4'] = Count['3'] << 4;
		Count['5'] = Count['4'] << 4;
		Count['6'] = Count['5'] << 4;
		Count['7'] = Count['6'] << 4;
		Count['8'] = Count['7'] << 4;
		Count['9'] = Count['8'] << 4;
		Count['T'] = Count['9'] << 4;
		Count['J'] = Count['T'] << 4;
		Count['Q'] = Count['J'] << 4;
		Count['K'] = Count['Q'] << 4;
		Count['A'] = Count['K'] << 4;
	}
};

const CardBitFields cardBitFields;

class PokerGames
{
public:
	PokerGames(const char *fileName) : nrOfGames(0)
	{
		if((fd = open(fileName, O_RDONLY, 0644 )) == -1) perror("open");
		if(fstat(fd, &file_info) == -1) perror("fstat");

		addr = mmap(NULL, file_info.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
		if (addr == MAP_FAILED) perror("mmap");

		nrOfGames = file_info.st_size / sizeof(PokerGame);
		pokerGamesData = (PokerGame*)addr;
	}
	~PokerGames()
	{
		munmap(addr, file_info.st_size);
		close(fd);
	}
	PokerGame& GetGame(int nr)
	{
		return pokerGamesData[nr];
	}
	int nrOfGames;
private:
	void *addr;
	PokerGame *pokerGamesData;
	struct stat file_info;
	int fd;
};

template<typename T>
void PrintBits(std::string prefix, T data, int seperate)
{
	std::printf(prefix.c_str());
	size_t size = sizeof(T);
	for(int i = size*8-1; i >= 0; i--)
	{
		int val = (data >> i) & 1;
		std::printf("%d", val);
		if((i % seperate) == 0) std::printf(" ");
	}
	std::printf("\n"); 
}

enum
{
	ones = 0,
	pairs = 1,
	triples = 2,
	quads = 3
};

uint64_t compress(uint64_t handValue[])
{
	uint64_t total = 0;
	for(int j=0; j < 4; j++)
	{ 
		uint64_t compressed = 0;
		for(int i=0; i < 14; i++)
		{
			compressed += (handValue[j] & ( 0x1ULL << (14 - i) * 4));
			compressed = compressed >> 3;
		}
		total += compressed << (14 * j);
	}
	return total;
}

uint64_t calculate_value(PokerGame::PlayerHand &hand)
{

	const uint64_t filter4 = 0x4444444444444444ULL;
	const uint64_t filter2 = 0x2222222222222222ULL;
	const uint64_t filter1 = 0x1111111111111111ULL;

	uint64_t handValue = cardBitFields.Count[hand.card[0].rank]
	          + cardBitFields.Count[hand.card[1].rank]
	          + cardBitFields.Count[hand.card[2].rank]
	          + cardBitFields.Count[hand.card[3].rank]
	          + cardBitFields.Count[hand.card[4].rank];

	uint64_t filtered[4];
	filtered[quads] = (handValue & filter4) >> 2;
	filtered[triples] = handValue & (handValue >> 1);
	filtered[ones] = (handValue & filter1) ^ filtered[triples];
	filtered[pairs] = ((handValue & filter2) >> 1) ^ filtered[triples];
		
	uint64_t compressed = compress(filtered);
	
	return compressed;
}

enum handrankbits
{
	chucknorris = 63,
	straightflush = 60,
	fourofakind = 59,
	fullhouse = 58,
	flush = 57,
	straight = 56
};

uint64_t check_for_straight(uint64_t handvalue)
{
	const uint64_t acemask = 0x1ULL << 13;
	uint64_t tmphandvalue = handvalue + ((handvalue & acemask) >> 13);
	
	tmphandvalue = tmphandvalue & (tmphandvalue >> 2);
	tmphandvalue = tmphandvalue & (tmphandvalue >> 1);
	tmphandvalue = tmphandvalue & (tmphandvalue >> 1);

	uint64_t isstraight = (tmphandvalue & 0x3FFF) > 0;

	// first bit is high when low straight
	uint64_t firstbit = tmphandvalue & 0x1ULL;
	// if low straight remove A and let 1 live
	handvalue = handvalue ^ (firstbit << 13);
	// add firstbit
	handvalue += firstbit;

	// potentially set straightbit
	handvalue |= (isstraight << straight);

	return handvalue;
}

uint64_t check_for_fourofakind(uint64_t handvalue)
{
	const uint64_t fourofakindfilter = 0x3FFFULL << (14 * quads);
	// potentially set four of a kind bit
	handvalue |= (uint64_t)((handvalue & fourofakindfilter) > 0) << fourofakind; 
	return handvalue;
}

uint64_t check_for_fullhouse(uint64_t handvalue)
{
	const uint64_t pairfilter = 0x3FFFULL << (14 * pairs);
	const uint64_t triplefilter = 0x3FFFULL << (14 * triples);
	handvalue |= (uint64_t)(((handvalue & pairfilter) > 0) & ((handvalue & triplefilter) > 0)) << fullhouse;
	return handvalue;
}

uint64_t check_for_flush(uint64_t handvalue, PokerGame::PlayerHand &hand)
{
	handvalue |= (uint64_t)((hand.card[0].suit == hand.card[1].suit) &
	(hand.card[0].suit == hand.card[2].suit) &
	(hand.card[0].suit == hand.card[3].suit) &
	(hand.card[0].suit == hand.card[4].suit)) << flush ;
	return handvalue;
}

uint64_t check_for_straightflush(uint64_t handvalue)
{
	const uint64_t straightfilter = 0x1ULL << straight;
	const uint64_t flushfilter = 0x1ULL << flush;
	handvalue |= (uint64_t)(((handvalue & straightfilter) > 0) & ((handvalue & flushfilter) > 0)) << straightflush;
	return handvalue;
}

// use lowest bit of threeofakind to indicate a twopair
uint64_t check_for_twopair(uint64_t handvalue)
{
	uint64_t pairsection = handvalue & (0x3FFFULL << (14 * pairs));
	pairsection = pairsection >> (14 * pairs);

	uint32_t counter = 0;
	for(int i=0; i < 14; i++)
	{
		counter += pairsection & 0x1ULL;
		pairsection = pairsection >> 1;
	}
	handvalue += ((uint64_t)(counter > 1)) << (14 * triples);
	return handvalue;
}

uint64_t calculate_handvalue(PokerGame::PlayerHand &hand)
{
	uint64_t handvalue = calculate_value(hand);
	handvalue = check_for_straight(handvalue);
	handvalue = check_for_fourofakind(handvalue);
	handvalue = check_for_fullhouse(handvalue);
	handvalue = check_for_flush(handvalue, hand);
	handvalue = check_for_straightflush(handvalue);
	handvalue = check_for_twopair(handvalue);
	return handvalue;
}


class Timer
{
public:
	Timer(std::string timerName)
	: timerName(timerName)
	{
		clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &starttime); 
	}
	~Timer()
	{
		clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &stoptime);
		std::printf("%s: %d\n", timerName.c_str(), stoptime.tv_nsec - starttime.tv_nsec);
	}
private:
	std::string timerName;
	timespec starttime, stoptime;
};

int main()
{
	Timer timer("Total time");
	PokerGames headsUpGames("poker.txt");
	int playerOneWins = 0;
	for(int i=0; i < headsUpGames.nrOfGames; i++)
	{
		PokerGame game = headsUpGames.GetGame(i);
		playerOneWins += (calculate_handvalue(game.playerHand[0]) > calculate_handvalue(game.playerHand[1]));
	} 

	std::printf("Player 1 wins: %d\n", playerOneWins);
	return 0;
}

