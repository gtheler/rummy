#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Compile-time variant defaults.
// You can override any runtime config using "config <clave> <valor>" commands.
// Example:
//   g++ -std=c++20 -O2 src/main.cpp -o rummy-cli -DRUMMY_VARIANT_GIN
#if defined(RUMMY_VARIANT_GIN)
static constexpr int DEFAULT_PLAYERS = 2;
static constexpr int DEFAULT_DECKS = 1;
static constexpr int DEFAULT_PRINTED_JOKERS = 0;
static constexpr int DEFAULT_HAND_SIZE = 10;
static constexpr bool DEFAULT_ENABLE_WILD = false;
static constexpr bool DEFAULT_ALLOW_WRAP = false;
static constexpr const char* DEFAULT_VARIANT_NAME = "gin";
#elif defined(RUMMY_VARIANT_500)
static constexpr int DEFAULT_PLAYERS = 4;
static constexpr int DEFAULT_DECKS = 2;
static constexpr int DEFAULT_PRINTED_JOKERS = 0;
static constexpr int DEFAULT_HAND_SIZE = 13;
static constexpr bool DEFAULT_ENABLE_WILD = false;
static constexpr bool DEFAULT_ALLOW_WRAP = false;
static constexpr const char* DEFAULT_VARIANT_NAME = "rummy500";
#else
// Default: Indian-like 13 card rummy profile.
static constexpr int DEFAULT_PLAYERS = 4;
static constexpr int DEFAULT_DECKS = 2;
static constexpr int DEFAULT_PRINTED_JOKERS = 2;
static constexpr int DEFAULT_HAND_SIZE = 13;
static constexpr bool DEFAULT_ENABLE_WILD = true;
static constexpr bool DEFAULT_ALLOW_WRAP = false;
static constexpr const char* DEFAULT_VARIANT_NAME = "indian13";
#endif

enum class Suit { CLUBS = 0, DIAMONDS = 1, HEARTS = 2, SPADES = 3, NONE = 4 };

static char suitToChar(Suit s) {
    switch (s) {
        case Suit::CLUBS: return 'N';      // Negro
        case Suit::DIAMONDS: return 'R';   // Rojo
        case Suit::HEARTS: return 'M';     // Naranja
        case Suit::SPADES: return 'C';     // Celeste
        default: return '?';
    }
}

struct Card {
    int rank = -1;  // A=0 ... K=12
    Suit suit = Suit::NONE;
    bool printedJoker = false;

    std::string faceKey() const {
        if (printedJoker) return "JO";
        return std::to_string(rank + 1) + suitToChar(suit);
    }

    std::string toString(bool markWild = false) const {
        if (printedJoker) return "JO";
        if (!markWild) return faceKey();
        return faceKey() + "*";
    }
};

struct Config {
    std::string variantName = "rummikub";
    int numPlayers = 2;
    int numDecks = 2;
    int printedJokersPerDeck = 1;
    bool enableWildRank = false;
    int wildRank = 0;  // A
    bool allowAKWrap = false;
    int maxJokersPerMeld = 2;
    int baseHandSize = 14;
    int simulations = 300;
    int horizonTurns = 2;
    unsigned int seed = 0;
    bool outputParseable = true;
    int outputTopN = 3;
};

struct Candidate {
    std::string label;
    bool takeDiscard = false;
    std::optional<Card> forcedDiscard;
    double winRate = 0.0;
    double improveRate = 0.0;
    double avgDeadwood = 0.0;
    int immediateDeadwood = 0;
};

struct GameState {
    Config cfg;
    std::vector<Card> myHand;
    std::vector<std::vector<Card>> tableMelds;
    std::vector<Card> discardPile;
    std::unordered_map<std::string, int> seen;
    std::vector<std::string> history;

    void clear() {
        myHand.clear();
        tableMelds.clear();
        discardPile.clear();
        seen.clear();
        history.clear();
    }
};

static std::unordered_map<std::string, int> g_deadwoodMemo;

static std::string lowerCopy(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static std::string normalizeToken(const std::string& token) {
    std::string out;
    for (char c : token) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '*' || c == '_') out.push_back(c);
    }
    return out;
}

static std::vector<std::string> splitTokens(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string t;
    while (iss >> t) {
        std::string n = normalizeToken(t);
        if (!n.empty()) tokens.push_back(n);
    }
    return tokens;
}

static int rankFromChar(char r) {
    switch (std::toupper(static_cast<unsigned char>(r))) {
        case 'A': return 0;
        case '2': return 1;
        case '3': return 2;
        case '4': return 3;
        case '5': return 4;
        case '6': return 5;
        case '7': return 6;
        case '8': return 7;
        case '9': return 8;
        case 'T': return 9;
        case 'J': return 10;
        case 'Q': return 11;
        case 'K': return 12;
        default: return -1;
    }
}

static Suit suitFromChar(char s) {
    switch (std::toupper(static_cast<unsigned char>(s))) {
        case 'N': return Suit::CLUBS;
        case 'R': return Suit::DIAMONDS;
        case 'M': return Suit::HEARTS;
        case 'C': return Suit::SPADES;
        case 'D': return Suit::DIAMONDS;
        case 'H': return Suit::HEARTS;
        case 'S': return Suit::SPADES;
        default: return Suit::NONE;
    }
}

static std::optional<Card> parseCard(const std::string& raw) {
    std::string t = raw;
    if (t.empty()) return std::nullopt;
    for (char& c : t) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    if (t == "JO" || t == "JOKER" || t == "X") {
        Card c;
        c.printedJoker = true;
        return c;
    }

    if (t.size() >= 2) {
        Suit s = suitFromChar(t.back());
        if (s != Suit::NONE) {
            std::string rankToken = t.substr(0, t.size() - 1);
            int r = -1;

            bool isNumeric = !rankToken.empty();
            for (char ch : rankToken) {
                if (!std::isdigit(static_cast<unsigned char>(ch))) {
                    isNumeric = false;
                    break;
                }
            }

            if (isNumeric) {
                int v = std::stoi(rankToken);
                if (v >= 1 && v <= 13) r = v - 1;
            } else if (rankToken.size() == 1) {
                r = rankFromChar(rankToken[0]);
            }

            if (r >= 0) {
                Card c;
                c.rank = r;
                c.suit = s;
                return c;
            }
        }
    }

    return std::nullopt;
}

static bool isJokerLike(const Card& c, const Config& cfg) {
    if (c.printedJoker) return true;
    return cfg.enableWildRank && c.rank == cfg.wildRank;
}

static int cardPoints(const Card& c, const Config& cfg) {
    if (isJokerLike(c, cfg)) return 0;
    if (c.rank == 0) return 1;
    if (c.rank >= 9) return 10;
    return c.rank + 1;
}

static bool removeOneByFace(std::vector<Card>& cards, const Card& target) {
    auto it = std::find_if(cards.begin(), cards.end(), [&](const Card& c) {
        return c.printedJoker == target.printedJoker && c.rank == target.rank && c.suit == target.suit;
    });
    if (it == cards.end()) return false;
    cards.erase(it);
    return true;
}

static std::vector<Card> buildFullDeck(const Config& cfg) {
    std::vector<Card> out;
    out.reserve(cfg.numDecks * (52 + cfg.printedJokersPerDeck));
    for (int d = 0; d < cfg.numDecks; ++d) {
        for (int s = 0; s < 4; ++s) {
            for (int r = 0; r < 13; ++r) {
                Card c;
                c.rank = r;
                c.suit = static_cast<Suit>(s);
                out.push_back(c);
            }
        }
        for (int j = 0; j < cfg.printedJokersPerDeck; ++j) {
            Card c;
            c.printedJoker = true;
            out.push_back(c);
        }
    }
    return out;
}

static std::vector<Card> remainingUnknownPool(const GameState& st) {
    std::vector<Card> full = buildFullDeck(st.cfg);
    std::unordered_map<std::string, int> seen = st.seen;
    std::vector<Card> rem;
    rem.reserve(full.size());
    for (const Card& c : full) {
        const std::string k = c.faceKey();
        auto it = seen.find(k);
        if (it != seen.end() && it->second > 0) {
            --(it->second);
            continue;
        }
        rem.push_back(c);
    }
    return rem;
}

static bool runCheckNoWrap(const std::vector<int>& ranks, int jokers) {
    if (ranks.empty()) return false;
    std::vector<int> rr = ranks;
    std::sort(rr.begin(), rr.end());
    rr.erase(std::unique(rr.begin(), rr.end()), rr.end());
    if (static_cast<int>(rr.size()) != static_cast<int>(ranks.size())) return false;

    int gaps = 0;
    for (size_t i = 1; i < rr.size(); ++i) {
        gaps += (rr[i] - rr[i - 1] - 1);
    }
    return gaps <= jokers;
}

static bool runCheckWrap(const std::vector<int>& ranks, int jokers) {
    if (ranks.empty()) return false;
    std::set<int> uniq(ranks.begin(), ranks.end());
    if (uniq.size() != ranks.size()) return false;

    std::vector<int> rr(uniq.begin(), uniq.end());
    for (int start = 0; start < 13; ++start) {
        std::vector<int> shifted;
        shifted.reserve(rr.size());
        for (int r : rr) shifted.push_back((r - start + 13) % 13);
        std::sort(shifted.begin(), shifted.end());

        int gaps = 0;
        for (size_t i = 1; i < shifted.size(); ++i) gaps += shifted[i] - shifted[i - 1] - 1;
        if (gaps <= jokers) return true;
    }
    return false;
}

static bool isSet(const std::vector<Card>& cards, const Config& cfg) {
    if (cards.size() < 3) return false;
    int jokers = 0;
    int rank = -1;
    for (const Card& c : cards) {
        if (isJokerLike(c, cfg)) {
            ++jokers;
            continue;
        }
        if (rank == -1) rank = c.rank;
        else if (c.rank != rank) return false;
    }
    if (jokers > cfg.maxJokersPerMeld) return false;
    return rank != -1;
}

static bool isRun(const std::vector<Card>& cards, const Config& cfg) {
    if (cards.size() < 3) return false;
    int jokers = 0;
    Suit suit = Suit::NONE;
    std::vector<int> ranks;
    for (const Card& c : cards) {
        if (isJokerLike(c, cfg)) {
            ++jokers;
            continue;
        }
        if (suit == Suit::NONE) suit = c.suit;
        else if (c.suit != suit) return false;
        ranks.push_back(c.rank);
    }
    if (jokers > cfg.maxJokersPerMeld) return false;
    if (ranks.empty()) return false;
    if (cfg.allowAKWrap) return runCheckWrap(ranks, jokers);
    return runCheckNoWrap(ranks, jokers);
}

static bool isValidMeld(const std::vector<Card>& cards, const Config& cfg) {
    return isSet(cards, cfg) || isRun(cards, cfg);
}

static int minDeadwood(const std::vector<Card>& hand, const Config& cfg) {
    const int n = static_cast<int>(hand.size());
    if (n == 0) return 0;
    if (n > 20) {
        int sum = 0;
        for (const Card& c : hand) sum += cardPoints(c, cfg);
        return sum;
    }

    std::vector<Card> canonical = hand;
    std::sort(canonical.begin(), canonical.end(), [](const Card& a, const Card& b) {
        if (a.printedJoker != b.printedJoker) return a.printedJoker < b.printedJoker;
        if (a.rank != b.rank) return a.rank < b.rank;
        return static_cast<int>(a.suit) < static_cast<int>(b.suit);
    });

    std::ostringstream keyBuilder;
    keyBuilder << cfg.enableWildRank << '|' << cfg.wildRank << '|'
               << cfg.allowAKWrap << '|' << cfg.maxJokersPerMeld << '|';
    for (const Card& c : canonical) keyBuilder << c.faceKey() << ',';
    const std::string memoKey = keyBuilder.str();
    auto memoIt = g_deadwoodMemo.find(memoKey);
    if (memoIt != g_deadwoodMemo.end()) return memoIt->second;

    std::vector<int> points(n, 0);
    int total = 0;
    for (int i = 0; i < n; ++i) {
        points[i] = cardPoints(hand[i], cfg);
        total += points[i];
    }

    std::vector<std::pair<int, int>> validMasks;  // mask -> points covered
    const int maxMask = 1 << n;
    for (int mask = 1; mask < maxMask; ++mask) {
        if (__builtin_popcount(static_cast<unsigned int>(mask)) < 3) continue;
        std::vector<Card> subset;
        subset.reserve(n);
        int covered = 0;
        for (int i = 0; i < n; ++i) {
            if (mask & (1 << i)) {
                subset.push_back(hand[i]);
                covered += points[i];
            }
        }
        if (isValidMeld(subset, cfg)) validMasks.push_back({mask, covered});
    }

    std::vector<int> memo(maxMask, -1);
    std::function<int(int)> bestCover = [&](int usedMask) -> int {
        int& ans = memo[usedMask];
        if (ans != -1) return ans;
        ans = 0;
        for (const auto& mv : validMasks) {
            int m = mv.first;
            if ((usedMask & m) != 0) continue;
            ans = std::max(ans, mv.second + bestCover(usedMask | m));
        }
        return ans;
    };

    int result = total - bestCover(0);
    g_deadwoodMemo[memoKey] = result;
    return result;
}

static bool canLayoffCardToMeld(const Card& c, const std::vector<Card>& meld, const Config& cfg) {
    std::vector<Card> ext = meld;
    ext.push_back(c);
    return isValidMeld(ext, cfg);
}

static int applyGreedyLayoff(std::vector<Card>& hand, std::vector<std::vector<Card>>& table, const Config& cfg) {
    int laid = 0;
    bool progressed = true;
    while (progressed) {
        progressed = false;
        for (size_t i = 0; i < hand.size(); ++i) {
            for (size_t m = 0; m < table.size(); ++m) {
                if (canLayoffCardToMeld(hand[i], table[m], cfg)) {
                    table[m].push_back(hand[i]);
                    hand.erase(hand.begin() + static_cast<long>(i));
                    ++laid;
                    progressed = true;
                    goto NEXT_PASS;
                }
            }
        }
        NEXT_PASS:;
    }
    return laid;
}

[[maybe_unused]] static std::optional<Card> chooseBestDiscard(const std::vector<Card>& hand,
                                                             const std::vector<std::vector<Card>>& table,
                                                             const Config& cfg) {
    if (hand.empty()) return std::nullopt;

    int bestDeadwood = std::numeric_limits<int>::max();
    int bestPoints = -1;
    std::optional<Card> best;

    for (size_t i = 0; i < hand.size(); ++i) {
        std::vector<Card> h = hand;
        Card d = h[i];
        h.erase(h.begin() + static_cast<long>(i));

        std::vector<std::vector<Card>> t = table;
        applyGreedyLayoff(h, t, cfg);
        int dw = minDeadwood(h, cfg);
        int p = cardPoints(d, cfg);
        if (!best || dw < bestDeadwood || (dw == bestDeadwood && p > bestPoints)) {
            best = d;
            bestDeadwood = dw;
            bestPoints = p;
        }
    }

    return best;
}

static std::optional<Card> chooseFastDiscard(const std::vector<Card>& hand, const Config& cfg) {
    if (hand.empty()) return std::nullopt;

    size_t bestIdx = 0;
    int bestPoints = -1;
    for (size_t i = 0; i < hand.size(); ++i) {
        int p = cardPoints(hand[i], cfg);
        if (p > bestPoints) {
            bestPoints = p;
            bestIdx = i;
        }
    }
    return hand[bestIdx];
}

static std::string cardsToString(const std::vector<Card>& cards, const Config& cfg) {
    std::ostringstream out;
    for (size_t i = 0; i < cards.size(); ++i) {
        if (i) out << ' ';
        out << cards[i].toString(cfg.enableWildRank && cards[i].rank == cfg.wildRank && !cards[i].printedJoker);
    }
    return out.str();
}

static std::string buildRecommendedCommand(const GameState& st, const Candidate& c) {
    if (c.takeDiscard && !st.discardPile.empty()) {
        std::string discard = c.forcedDiscard.has_value() ? c.forcedDiscard->faceKey() : "<CARTA_A_DESCARTAR>";
        return "tomo_descarte\\ndescarto " + discard;
    }

    // Al tomar del mazo, la carta real llega desde la UI/fuente externa.
    return "recibo <CARTA_DEL_MAZO>\\nturno";
}

static std::string escapeForQuoted(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char ch : s) {
        if (ch == '\\') out += "\\\\";
        else if (ch == '"') out += "\\\"";
        else out.push_back(ch);
    }
    return out;
}

struct SimOutcome {
    double win = 0.0;
    double improved = 0.0;
    double deadwood = 0.0;
};

struct LayoffSuggestion {
    Card card;
    std::vector<Card> targetMeldBefore;
};

struct ImmediatePlan {
    std::vector<std::vector<Card>> meldsToLayDown;
    std::vector<LayoffSuggestion> layoffMoves;
};

struct RearrangementPlan {
    bool hasPlan = false;
    size_t sourceMeldIndex = 0;
    size_t destMeldIndex = 0;
    Card movedCard;
    ImmediatePlan followUp;
    int resultingDeadwood = std::numeric_limits<int>::max();
    int totalCardsPlayed = 0;
};

static ImmediatePlan buildImmediatePlan(const GameState& st);
static int deadwoodAfterLayoff(const std::vector<Card>& hand, const std::vector<std::vector<Card>>& table,
                               const Config& cfg);

static Card drawRandom(std::vector<Card>& pool, std::mt19937& rng) {
    std::uniform_int_distribution<size_t> dist(0, pool.size() - 1);
    size_t idx = dist(rng);
    Card c = pool[idx];
    pool[idx] = pool.back();
    pool.pop_back();
    return c;
}

static std::string cardsToFaceList(const std::vector<Card>& cards) {
    std::ostringstream out;
    for (size_t i = 0; i < cards.size(); ++i) {
        if (i) out << ' ';
        out << cards[i].faceKey();
    }
    return out.str();
}

static std::vector<Card> orderedMeldForOutput(const std::vector<Card>& meld, const Config& cfg) {
    if (!isRun(meld, cfg)) return meld;

    std::vector<Card> regular;
    std::vector<Card> jokers;
    regular.reserve(meld.size());
    jokers.reserve(meld.size());

    for (const Card& c : meld) {
        if (isJokerLike(c, cfg)) jokers.push_back(c);
        else regular.push_back(c);
    }

    std::sort(regular.begin(), regular.end(), [](const Card& a, const Card& b) {
        if (a.rank != b.rank) return a.rank < b.rank;
        return static_cast<int>(a.suit) < static_cast<int>(b.suit);
    });

    std::vector<Card> ordered = regular;
    ordered.insert(ordered.end(), jokers.begin(), jokers.end());
    return ordered;
}

static bool moveCardBetweenMelds(const std::vector<Card>& source, const std::vector<Card>& dest, const Card& moved,
                                 const Config& cfg, std::vector<Card>& sourceOut, std::vector<Card>& destOut) {
    sourceOut = source;
    if (!removeOneByFace(sourceOut, moved)) return false;
    if (sourceOut.size() < 3 || !isValidMeld(sourceOut, cfg)) return false;

    destOut = dest;
    destOut.push_back(moved);
    if (!isValidMeld(destOut, cfg)) return false;
    return true;
}

static int simulateImmediatePlanDeadwood(const GameState& baseState, const ImmediatePlan& plan) {
    std::vector<Card> hand = baseState.myHand;
    std::vector<std::vector<Card>> table = baseState.tableMelds;

    for (const auto& meld : plan.meldsToLayDown) {
        for (const Card& c : meld) {
            if (!removeOneByFace(hand, c)) return std::numeric_limits<int>::max();
        }
        table.push_back(meld);
    }

    applyGreedyLayoff(hand, table, baseState.cfg);
    return minDeadwood(hand, baseState.cfg);
}

static RearrangementPlan buildRearrangementPlan(const GameState& st) {
    RearrangementPlan best;
    if (st.tableMelds.size() < 2 || st.myHand.empty()) return best;

    const int baselineDeadwood = deadwoodAfterLayoff(st.myHand, st.tableMelds, st.cfg);

    for (size_t sourceIdx = 0; sourceIdx < st.tableMelds.size(); ++sourceIdx) {
        const auto& source = st.tableMelds[sourceIdx];
        for (size_t cardIdx = 0; cardIdx < source.size(); ++cardIdx) {
            const Card moved = source[cardIdx];
            for (size_t destIdx = 0; destIdx < st.tableMelds.size(); ++destIdx) {
                if (destIdx == sourceIdx) continue;

                std::vector<Card> sourceOut;
                std::vector<Card> destOut;
                if (!moveCardBetweenMelds(source, st.tableMelds[destIdx], moved, st.cfg, sourceOut, destOut)) continue;

                GameState temp = st;
                temp.tableMelds[sourceIdx] = sourceOut;
                temp.tableMelds[destIdx] = destOut;

                ImmediatePlan followUp = buildImmediatePlan(temp);
                int resultingDeadwood = simulateImmediatePlanDeadwood(temp, followUp);
                if (resultingDeadwood == std::numeric_limits<int>::max()) continue;

                const int totalCardsPlayed = static_cast<int>(followUp.meldsToLayDown.size() + followUp.layoffMoves.size());
                if (totalCardsPlayed == 0) continue;

                bool better = !best.hasPlan
                    || totalCardsPlayed > best.totalCardsPlayed
                    || (totalCardsPlayed == best.totalCardsPlayed && resultingDeadwood < best.resultingDeadwood)
                    || (totalCardsPlayed == best.totalCardsPlayed && resultingDeadwood == best.resultingDeadwood
                        && resultingDeadwood < baselineDeadwood);

                if (better) {
                    best.hasPlan = true;
                    best.sourceMeldIndex = sourceIdx;
                    best.destMeldIndex = destIdx;
                    best.movedCard = moved;
                    best.followUp = std::move(followUp);
                    best.resultingDeadwood = resultingDeadwood;
                    best.totalCardsPlayed = totalCardsPlayed;
                }
            }
        }
    }

    return best;
}

static std::string buildRearrangementPlanCommand(const RearrangementPlan& plan, const Config& cfg) {
    if (!plan.hasPlan) return {};

    std::vector<std::string> commands;
    commands.push_back("muevo " + plan.movedCard.faceKey()
                       + " de M" + std::to_string(plan.sourceMeldIndex + 1)
                       + " a M" + std::to_string(plan.destMeldIndex + 1));

    for (const auto& meld : plan.followUp.meldsToLayDown) {
        const std::vector<Card> ordered = orderedMeldForOutput(meld, cfg);
        commands.push_back("bajo " + cardsToFaceList(ordered));
    }

    for (const auto& move : plan.followUp.layoffMoves) {
        commands.push_back("cuelo " + move.card.faceKey() + " en " + cardsToFaceList(move.targetMeldBefore));
    }

    std::ostringstream out;
    for (size_t i = 0; i < commands.size(); ++i) {
        if (i) out << "\\n";
        out << commands[i];
    }
    return out.str();
}

static std::optional<size_t> parseMeldIndexToken(const std::string& token) {
    std::string t = lowerCopy(token);
    if (t.size() < 2 || t[0] != 'm') return std::nullopt;
    std::string digits = t.substr(1);
    if (digits.empty()) return std::nullopt;
    for (char ch : digits) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) return std::nullopt;
    }
    try {
        size_t value = static_cast<size_t>(std::stoul(digits));
        if (value == 0) return std::nullopt;
        return value - 1;
    } catch (...) {
        return std::nullopt;
    }
}

static ImmediatePlan buildImmediatePlan(const GameState& st) {
    ImmediatePlan plan;
    const std::vector<Card>& hand = st.myHand;
    const int n = static_cast<int>(hand.size());
    if (n == 0) return plan;

    std::vector<int> points(n, 0);
    for (int i = 0; i < n; ++i) points[i] = cardPoints(hand[i], st.cfg);

    std::vector<std::pair<int, int>> validMasks;  // mask -> covered points
    const int maxMask = 1 << n;
    for (int mask = 1; mask < maxMask; ++mask) {
        if (__builtin_popcount(static_cast<unsigned int>(mask)) < 3) continue;
        std::vector<Card> subset;
        subset.reserve(n);
        int covered = 0;
        for (int i = 0; i < n; ++i) {
            if (mask & (1 << i)) {
                subset.push_back(hand[i]);
                covered += points[i];
            }
        }
        if (isValidMeld(subset, st.cfg)) validMasks.push_back({mask, covered});
    }

    std::vector<int> memo(maxMask, -1);
    std::function<int(int)> bestCover = [&](int usedMask) -> int {
        int& ans = memo[usedMask];
        if (ans != -1) return ans;
        ans = 0;
        for (const auto& mv : validMasks) {
            int m = mv.first;
            if ((usedMask & m) != 0) continue;
            ans = std::max(ans, mv.second + bestCover(usedMask | m));
        }
        return ans;
    };

    (void)bestCover(0);

    int usedMask = 0;
    while (true) {
        int current = memo[usedMask];
        if (current <= 0) break;

        bool picked = false;
        for (const auto& mv : validMasks) {
            int m = mv.first;
            if ((usedMask & m) != 0) continue;

            int candidate = mv.second + memo[usedMask | m];
            if (candidate == current) {
                std::vector<Card> meld;
                for (int i = 0; i < n; ++i) {
                    if (m & (1 << i)) meld.push_back(hand[i]);
                }
                plan.meldsToLayDown.push_back(meld);
                usedMask |= m;
                picked = true;
                break;
            }
        }
        if (!picked) break;
    }

    std::vector<Card> remainingHand;
    remainingHand.reserve(hand.size());
    for (int i = 0; i < n; ++i) {
        if ((usedMask & (1 << i)) == 0) remainingHand.push_back(hand[i]);
    }

    std::vector<std::vector<Card>> table = st.tableMelds;
    for (const auto& meld : plan.meldsToLayDown) table.push_back(meld);

    bool progressed = true;
    while (progressed) {
        progressed = false;
        for (size_t i = 0; i < remainingHand.size(); ++i) {
            for (size_t m = 0; m < table.size(); ++m) {
                if (canLayoffCardToMeld(remainingHand[i], table[m], st.cfg)) {
                    LayoffSuggestion move;
                    move.card = remainingHand[i];
                    move.targetMeldBefore = table[m];
                    plan.layoffMoves.push_back(move);

                    table[m].push_back(remainingHand[i]);
                    remainingHand.erase(remainingHand.begin() + static_cast<long>(i));
                    progressed = true;
                    goto NEXT_PASS;
                }
            }
        }
        NEXT_PASS:;
    }

    return plan;
}

static std::string buildImmediatePlanCommand(const ImmediatePlan& plan, const Config& cfg) {
    std::vector<std::string> commands;
    commands.reserve(plan.meldsToLayDown.size() + plan.layoffMoves.size());

    for (const auto& meld : plan.meldsToLayDown) {
        const std::vector<Card> ordered = orderedMeldForOutput(meld, cfg);
        commands.push_back("bajo " + cardsToFaceList(ordered));
    }
    for (const auto& move : plan.layoffMoves) {
        commands.push_back("cuelo " + move.card.faceKey() + " en " + cardsToFaceList(move.targetMeldBefore));
    }

    std::ostringstream out;
    for (size_t i = 0; i < commands.size(); ++i) {
        if (i) out << "\\n";
        out << commands[i];
    }
    return out.str();
}

static int deadwoodAfterLayoff(const std::vector<Card>& hand, const std::vector<std::vector<Card>>& table,
                               const Config& cfg) {
    std::vector<Card> h = hand;
    std::vector<std::vector<Card>> t = table;
    applyGreedyLayoff(h, t, cfg);
    return minDeadwood(h, cfg);
}

static SimOutcome simulateCandidate(const GameState& st, const Candidate& cand, std::mt19937& rng, int baselineDeadwood) {
    std::vector<Card> pool = remainingUnknownPool(st);
    std::vector<Card> myHand = st.myHand;
    std::vector<std::vector<Card>> table = st.tableMelds;

    if (cand.takeDiscard) {
        if (st.discardPile.empty()) return {0.0, 0.0, 999.0};
        myHand.push_back(st.discardPile.back());
    } else {
        if (pool.empty()) return {0.0, 0.0, 999.0};
        myHand.push_back(drawRandom(pool, rng));
    }

    Card discard;
    if (cand.forcedDiscard.has_value()) {
        discard = *cand.forcedDiscard;
    } else {
        // Fast simulation path: use cheap discard heuristic to keep Monte Carlo responsive.
        auto best = chooseFastDiscard(myHand, st.cfg);
        if (!best.has_value()) return {0.0, 0.0, 999.0};
        discard = *best;
    }

    if (!removeOneByFace(myHand, discard)) return {0.0, 0.0, 999.0};

    int players = std::max(2, st.cfg.numPlayers);
    std::vector<std::vector<Card>> oppHands(players - 1);
    for (int i = 0; i < players - 1; ++i) {
        for (int k = 0; k < st.cfg.baseHandSize && !pool.empty(); ++k) oppHands[i].push_back(drawRandom(pool, rng));
    }

    auto oppDiscardPolicy = [&](std::vector<Card>& h) {
        if (h.empty()) return;
        int idx = 0;
        int best = -1;
        for (size_t i = 0; i < h.size(); ++i) {
            int p = cardPoints(h[i], st.cfg);
            if (p > best) {
                best = p;
                idx = static_cast<int>(i);
            }
        }
        h.erase(h.begin() + idx);
    };

    for (int t = 0; t < st.cfg.horizonTurns; ++t) {
        for (auto& h : oppHands) {
            if (!pool.empty()) h.push_back(drawRandom(pool, rng));
            oppDiscardPolicy(h);
        }

        if (!pool.empty()) myHand.push_back(drawRandom(pool, rng));
        auto bestDiscard = chooseFastDiscard(myHand, st.cfg);
        if (bestDiscard.has_value()) removeOneByFace(myHand, *bestDiscard);
    }

    int myDw = deadwoodAfterLayoff(myHand, table, st.cfg);
    int better = 0;
    int equal = 0;
    for (auto& h : oppHands) {
        int d = minDeadwood(h, st.cfg);
        if (myDw < d) ++better;
        if (myDw == d) ++equal;
    }

    double win = 0.0;
    if (better == players - 1) win = 1.0;
    else if (better + equal == players - 1) win = 0.5;

    return {win, myDw < baselineDeadwood ? 1.0 : 0.0, static_cast<double>(myDw)};
}

static std::vector<Candidate> buildCandidates(const GameState& st) {
    std::vector<Candidate> out;

    // Option: take stock (unknown draw), then best discard policy in simulation.
    Candidate stock;
    stock.label = "TOMAR_MAZO (carta oculta, descarte optimizado)";
    stock.takeDiscard = false;
    out.push_back(stock);

    if (!st.discardPile.empty()) {
        Card top = st.discardPile.back();
        std::vector<Card> hand = st.myHand;
        hand.push_back(top);

        std::set<std::string> seenDiscardFaces;
        for (const Card& d : hand) {
            std::string k = d.faceKey();
            if (seenDiscardFaces.count(k)) continue;
            seenDiscardFaces.insert(k);

            std::vector<Card> h2 = hand;
            if (!removeOneByFace(h2, d)) continue;
            std::vector<std::vector<Card>> t2 = st.tableMelds;
            applyGreedyLayoff(h2, t2, st.cfg);

            Candidate c;
            c.takeDiscard = true;
            c.forcedDiscard = d;
            c.immediateDeadwood = minDeadwood(h2, st.cfg);
            c.label = "TOMAR_DESCARTE " + top.faceKey() + " y DESCARTAR " + d.faceKey();
            out.push_back(c);
        }
    }

    return out;
}

static void rankAndPrintRecommendations(const GameState& st) {
    if (st.myHand.empty()) {
        std::cout << "No hay cartas en mano. Usa 'recibo <carta>' o 'mano <cartas...>'.\n";
        return;
    }

    int baseline = deadwoodAfterLayoff(st.myHand, st.tableMelds, st.cfg);
    auto candidates = buildCandidates(st);
    ImmediatePlan immediatePlan = buildImmediatePlan(st);
    RearrangementPlan rearrangementPlan = buildRearrangementPlan(st);

    std::mt19937 rng;
    if (st.cfg.seed != 0) rng.seed(st.cfg.seed);
    else rng.seed(std::random_device{}());

    for (Candidate& c : candidates) {
        int sims = std::max(10, st.cfg.simulations);
        double win = 0.0;
        double imp = 0.0;
        double dead = 0.0;

        for (int i = 0; i < sims; ++i) {
            SimOutcome o = simulateCandidate(st, c, rng, baseline);
            win += o.win;
            imp += o.improved;
            dead += o.deadwood;
        }

        c.winRate = win / sims;
        c.improveRate = imp / sims;
        c.avgDeadwood = dead / sims;

        if (!c.forcedDiscard.has_value()) {
            std::vector<Card> h = st.myHand;
            std::vector<std::vector<Card>> t = st.tableMelds;
            applyGreedyLayoff(h, t, st.cfg);
            c.immediateDeadwood = minDeadwood(h, st.cfg);
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (std::fabs(a.winRate - b.winRate) > 1e-9) return a.winRate > b.winRate;
        if (std::fabs(a.improveRate - b.improveRate) > 1e-9) return a.improveRate > b.improveRate;
        return a.avgDeadwood < b.avgDeadwood;
    });

    if (!candidates.empty()) {
        const std::string cmd = buildRecommendedCommand(st, candidates.front());
        std::cout << "RECOMMENDED_PLAY \"" << escapeForQuoted(cmd) << "\"\n";
    }

    if (!immediatePlan.meldsToLayDown.empty() || !immediatePlan.layoffMoves.empty()) {
        const std::string immediateCmd = buildImmediatePlanCommand(immediatePlan, st.cfg);
        std::cout << "IMMEDIATE_PLAY \"" << escapeForQuoted(immediateCmd) << "\"\n";
    }

    if (rearrangementPlan.hasPlan) {
        const std::string rearrangeCmd = buildRearrangementPlanCommand(rearrangementPlan, st.cfg);
        std::cout << "REARRANGE_PLAY \"" << escapeForQuoted(rearrangeCmd) << "\"\n";
    }

    int topN = std::max(1, st.cfg.outputTopN);
    topN = std::min(topN, static_cast<int>(candidates.size()));

    std::vector<Card> h = st.myHand;
    std::vector<std::vector<Card>> t = st.tableMelds;
    int laid = applyGreedyLayoff(h, t, st.cfg);

    if (st.cfg.outputParseable) {
        std::cout << "RESULT version=1 mode=parseable";
        std::cout << " variant=" << st.cfg.variantName;
        std::cout << " players=" << st.cfg.numPlayers;
        std::cout << " decks=" << st.cfg.numDecks;
        std::cout << " sims=" << st.cfg.simulations;
        std::cout << " baseline_deadwood=" << baseline;
        std::cout << " layoff_cards=" << laid;
        std::cout << " top_n=" << topN << "\n";

        std::cout << "IMMEDIATE summary="
              << (!immediatePlan.meldsToLayDown.empty() || !immediatePlan.layoffMoves.empty() ? "available" : "none")
              << " melds=" << immediatePlan.meldsToLayDown.size()
              << " layoffs=" << immediatePlan.layoffMoves.size() << "\n";

        if (rearrangementPlan.hasPlan) {
            std::cout << "REARRANGE summary=available";
            std::cout << " source=M" << (rearrangementPlan.sourceMeldIndex + 1);
            std::cout << " dest=M" << (rearrangementPlan.destMeldIndex + 1);
            std::cout << " moved=" << rearrangementPlan.movedCard.faceKey();
            std::cout << " followup_melds=" << rearrangementPlan.followUp.meldsToLayDown.size();
            std::cout << " followup_layoffs=" << rearrangementPlan.followUp.layoffMoves.size();
            std::cout << " deadwood_after=" << rearrangementPlan.resultingDeadwood << "\n";
        } else {
            std::cout << "REARRANGE summary=none\n";
        }

        for (int i = 0; i < topN; ++i) {
            const Candidate& c = candidates[i];
            std::cout << "PLAY rank=" << (i + 1);
            std::cout << " take=" << (c.takeDiscard ? "discard" : "stock");
            if (c.takeDiscard && !st.discardPile.empty()) {
                std::cout << " take_card=" << st.discardPile.back().faceKey();
            }
            if (c.forcedDiscard.has_value()) {
                std::cout << " discard=" << c.forcedDiscard->faceKey();
            } else {
                std::cout << " discard=auto";
            }
            std::cout << " win=" << std::fixed << std::setprecision(6) << c.winRate;
            std::cout << " improve=" << std::fixed << std::setprecision(6) << c.improveRate;
            std::cout << " deadwood_expected=" << std::fixed << std::setprecision(6) << c.avgDeadwood;
            std::cout << " deadwood_immediate=" << c.immediateDeadwood;
            std::cout << " label=" << c.label;
            std::cout << "\n";
        }
        std::cout << "END\n";
    } else {
        std::cout << "=== RECOMENDACIONES ===\n";
        std::cout << "Variante=" << st.cfg.variantName
                  << " jugadores=" << st.cfg.numPlayers
                  << " mazos=" << st.cfg.numDecks
                  << " comodin_wild=" << (st.cfg.enableWildRank ? "si" : "no")
                  << " rank_wild=" << (st.cfg.wildRank + 1)
                  << " wraps=" << (st.cfg.allowAKWrap ? "si" : "no")
                  << " sims=" << st.cfg.simulations
                  << "\n";

        std::cout << "Deadwood base actual: " << baseline << "\n";

        if (!immediatePlan.meldsToLayDown.empty() || !immediatePlan.layoffMoves.empty()) {
            std::cout << "Tenes jugadas inmediatas en mesa ahora mismo:\n";
            for (const auto& meld : immediatePlan.meldsToLayDown) {
                const std::vector<Card> ordered = orderedMeldForOutput(meld, st.cfg);
                std::cout << "  - Bajo sugerido: bajo " << cardsToFaceList(ordered) << "\n";
            }
            for (const auto& move : immediatePlan.layoffMoves) {
                std::cout << "  - Colada sugerida: cuelo " << move.card.faceKey()
                          << " en " << cardsToFaceList(move.targetMeldBefore) << "\n";
            }
        }

        if (rearrangementPlan.hasPlan) {
            std::cout << "Tenes un reacomodo posible para habilitar mas jugadas:\n";
            std::cout << "  - Reacomodo sugerido: " << buildRearrangementPlanCommand(rearrangementPlan, st.cfg) << "\n";
        }

        for (int i = 0; i < topN; ++i) {
            const Candidate& c = candidates[i];
            std::cout << std::setw(2) << (i + 1) << ") " << c.label << "\n";
            std::cout << "    P(win aprox): " << std::fixed << std::setprecision(3) << c.winRate
                      << " | P(mejorar): " << c.improveRate
                      << " | Deadwood esperado: " << c.avgDeadwood
                      << " | Deadwood inmediato: " << c.immediateDeadwood << "\n";
        }

        if (laid > 0) {
            std::cout << "Sugerencia de colada inmediata: podrias colar " << laid
                      << " carta(s) en juegos de mesa antes/despues del descarte segun reglas de tu mesa.\n";
        }
    }
}

static void printState(const GameState& st) {
    std::cout << "Estado actual:\n";
    std::cout << "  Mano (" << st.myHand.size() << "): " << cardsToString(st.myHand, st.cfg) << "\n";
    std::cout << "  Descarte tope: ";
    if (st.discardPile.empty()) std::cout << "(vacio)\n";
    else std::cout << st.discardPile.back().toString() << "\n";

    std::cout << "  Juegos en mesa: " << st.tableMelds.size() << "\n";
    for (size_t i = 0; i < st.tableMelds.size(); ++i) {
        std::cout << "    M" << (i + 1) << ": " << cardsToString(st.tableMelds[i], st.cfg) << "\n";
    }

    std::cout << "  Vistas en historial: " << st.seen.size() << " caras distintas\n";
}

static void markSeen(GameState& st, const Card& c) {
    st.seen[c.faceKey()]++;
}

static int maxCopiesForCard(const Config& cfg, const Card& c) {
    if (c.printedJoker) return cfg.numDecks * cfg.printedJokersPerDeck;
    return cfg.numDecks;
}

static bool canMarkSeen(const GameState& st, const Card& c, int extraCopies = 1) {
    const int maxCopies = maxCopiesForCard(st.cfg, c);
    auto it = st.seen.find(c.faceKey());
    const int used = (it == st.seen.end()) ? 0 : it->second;
    return used + extraCopies <= maxCopies;
}

static bool tryMarkSeen(GameState& st, const Card& c) {
    if (!canMarkSeen(st, c)) return false;
    markSeen(st, c);
    return true;
}

static void unmarkSeen(GameState& st, const Card& c) {
    auto it = st.seen.find(c.faceKey());
    if (it == st.seen.end()) return;
    if (it->second <= 1) st.seen.erase(it);
    else --(it->second);
}

static void applyGamePreset(GameState& st, const std::string& value) {
    if (value == "rummikub") {
        st.cfg.variantName = "rummikub";
        st.cfg.numDecks = 2;
        // 2 mazos con 2 jokers totales => 1 joker impreso por mazo.
        st.cfg.printedJokersPerDeck = 1;
        st.cfg.enableWildRank = false;
        st.cfg.allowAKWrap = false;
        st.cfg.baseHandSize = 14;
        g_deadwoodMemo.clear();
        return;
    }

    if (value == "indian13") {
        st.cfg.variantName = "indian13";
        st.cfg.numPlayers = 4;
        st.cfg.numDecks = 2;
        st.cfg.printedJokersPerDeck = 2;
        st.cfg.enableWildRank = true;
        st.cfg.wildRank = 0;
        st.cfg.allowAKWrap = false;
        st.cfg.baseHandSize = 13;
        g_deadwoodMemo.clear();
    }
}

static bool handleConfig(GameState& st, const std::vector<std::string>& tk) {
    if (tk.size() < 3) return false;
    std::string key = lowerCopy(tk[1]);
    std::string value = lowerCopy(tk[2]);

    auto toInt = [&](const std::string& s) -> std::optional<int> {
        try {
            size_t pos = 0;
            int v = std::stoi(s, &pos);
            if (pos != s.size()) return std::nullopt;
            return v;
        } catch (...) {
            return std::nullopt;
        }
    };

    if (key == "jugadores") {
        auto v = toInt(value);
        if (v && *v >= 2 && *v <= 8) st.cfg.numPlayers = *v;
        else return false;
        g_deadwoodMemo.clear();
        return true;
    }
    if (key == "mazos") {
        auto v = toInt(value);
        if (v && *v >= 1 && *v <= 6) st.cfg.numDecks = *v;
        else return false;
        g_deadwoodMemo.clear();
        return true;
    }
    if (key == "jokers_impresos") {
        auto v = toInt(value);
        if (v && *v >= 0 && *v <= 8) st.cfg.printedJokersPerDeck = *v;
        else return false;
        g_deadwoodMemo.clear();
        return true;
    }
    if (key == "joker_formato") {
        if (value == "none") {
            st.cfg.enableWildRank = false;
            st.cfg.printedJokersPerDeck = 0;
            g_deadwoodMemo.clear();
            return true;
        }
        if (value == "printed") {
            st.cfg.enableWildRank = false;
            g_deadwoodMemo.clear();
            return true;
        }
        if (value == "printed_plus_wild" || value == "wild") {
            st.cfg.enableWildRank = true;
            g_deadwoodMemo.clear();
            return true;
        }
        return false;
    }
    if (key == "wild_rank") {
        int r = -1;
        bool isNumeric = !value.empty();
        for (char ch : value) {
            if (!std::isdigit(static_cast<unsigned char>(ch))) {
                isNumeric = false;
                break;
            }
        }
        if (isNumeric) {
            int v = std::stoi(value);
            if (v >= 1 && v <= 13) r = v - 1;
        } else if (value.size() == 1) {
            r = rankFromChar(value[0]);
        }
        if (r < 0) return false;
        st.cfg.wildRank = r;
        st.cfg.enableWildRank = true;
        g_deadwoodMemo.clear();
        return true;
    }
    if (key == "wraps") {
        st.cfg.allowAKWrap = (value == "on" || value == "si" || value == "true" || value == "1");
        g_deadwoodMemo.clear();
        return true;
    }
    if (key == "jokers_por_juego") {
        auto v = toInt(value);
        if (v && *v >= 0 && *v <= 4) st.cfg.maxJokersPerMeld = *v;
        else return false;
        g_deadwoodMemo.clear();
        return true;
    }
    if (key == "simulaciones") {
        auto v = toInt(value);
        if (v && *v >= 10 && *v <= 20000) st.cfg.simulations = *v;
        else return false;
        return true;
    }
    if (key == "output") {
        if (value == "human") {
            st.cfg.outputParseable = false;
            return true;
        }
        st.cfg.outputParseable = (value == "parseable" || value == "pipe" || value == "kv" || value == "script");
        return true;
    }
    if (key == "top") {
        auto v = toInt(value);
        if (v && *v >= 1 && *v <= 50) st.cfg.outputTopN = *v;
        else return false;
        return true;
    }
    if (key == "horizonte") {
        auto v = toInt(value);
        if (v && *v >= 1 && *v <= 15) st.cfg.horizonTurns = *v;
        else return false;
        return true;
    }
    if (key == "mano_inicial") {
        auto v = toInt(value);
        if (v && *v >= 7 && *v <= 20) st.cfg.baseHandSize = *v;
        else return false;
        return true;
    }
    if (key == "seed") {
        auto v = toInt(value);
        if (v && *v >= 0) st.cfg.seed = static_cast<unsigned int>(*v);
        else return false;
        return true;
    }
    if (key == "variante") {
        applyGamePreset(st, value);
        st.cfg.variantName = value;
        return true;
    }
    if (key == "game" || key == "juego") {
        applyGamePreset(st, value);
        return true;
    }

    return false;
}

static void printHelp() {
    std::cout
        << "Comandos principales:\n"
        << "  config <clave> <valor>\n"
        << "  mano <c1> <c2> ...                      # setea mano completa\n"
        << "  recibo <carta>                          # agrega carta a mano\n"
        << "  tomo_descarte                           # toma el tope del descarte\n"
        << "  descarto <carta>                        # descarta desde mano\n"
        << "  bajo <c1> <c2> <c3> [...]              # baja juego desde mano\n"
        << "  mesa <c1> <c2> <c3> [...]              # agrega juego visible en mesa\n"
        << "  otro_baja <jugadorN> <c1> <c2> <c3>... # otro jugador baja juego\n"
        << "  cuelo <carta> en <cartas_del_juego>     # intenta colar en juego existente\n"
        << "  otro_cuela <jugadorN> <carta> en <...>   # otro jugador agrega carta a juego\n"
        << "  descarte <carta>                        # informa tope del descarte visible\n"
        << "  sale <carta>                            # marca carta como vista en historial\n"
        << "  estado                                  # imprime estado\n"
        << "  turno                                   # calcula ranking de jugadas\n"
        << "  reset                                   # borra estado\n"
        << "  help                                    # ayuda\n"
        << "  fin                                     # salir\n"
        << "\n"
        << "Claves de config:\n"
        << "  jugadores, mazos, jokers_impresos, joker_formato, wild_rank, wraps,\n"
        << "  jokers_por_juego, simulaciones, output, top, horizonte, mano_inicial, seed, variante, juego\n"
        << "  presets: config juego rummikub (2 mazos, 2 jokers totales)\n"
        << "\n"
        << "Formato de ficha/carta:\n"
        << "  Numero+color: 1N..13N, 1R..13R, 1M..13M, 1C..13C (N=negro, R=rojo, M=naranja, C=celeste)\n"
        << "  Alias admitidos: T/J/Q/K + color (ej: TC, JR)\n"
        << "  Joker: JO\n";
}

static bool processCommandLine(GameState& st, const std::string& line, bool& shouldExit) {
    auto tk = splitTokens(line);
    if (tk.empty()) return true;

    std::string cmd = lowerCopy(tk[0]);

    if (cmd == "help" || cmd == "ayuda") {
        printHelp();
        return true;
    }

    if (cmd == "fin" || cmd == "exit" || cmd == "quit") {
        shouldExit = true;
        return true;
    }

    if (cmd == "reset") {
        st.clear();
        g_deadwoodMemo.clear();
        std::cout << "OK reset\n";
        return true;
    }

    if (cmd == "config") {
        if (handleConfig(st, tk)) std::cout << "OK config\n";
        else std::cout << "ERROR config invalida\n";
        return true;
    }

    if (cmd == "estado") {
        printState(st);
        return true;
    }

    if (cmd == "turno" || cmd == "recomendar") {
        rankAndPrintRecommendations(st);
        return true;
    }

    if (cmd == "mano") {
        const int requiredHandSize = st.cfg.baseHandSize;
        const int providedHandSize = static_cast<int>(tk.size()) - 1;
        if (providedHandSize != requiredHandSize) {
            std::cout << "ERROR mano invalida: se esperaban " << requiredHandSize
                      << " fichas y llegaron " << providedHandSize << "\n";
            std::cout << "ERROR mano rechazada: corrige e ingresa nuevamente\n";
            return true;
        }

        std::vector<Card> nextHand;
        nextHand.reserve(tk.size() > 1 ? tk.size() - 1 : 0);

        // Validate against seen state without current hand, so replacement is atomic.
        std::unordered_map<std::string, int> seenBase = st.seen;
        for (const Card& c : st.myHand) {
            auto it = seenBase.find(c.faceKey());
            if (it == seenBase.end()) continue;
            if (it->second <= 1) seenBase.erase(it);
            else --(it->second);
        }

        std::unordered_map<std::string, int> addedCounts;
        for (size_t i = 1; i < tk.size(); ++i) {
            auto c = parseCard(tk[i]);
            if (!c.has_value()) {
                std::cout << "ERROR carta invalida: " << tk[i] << "\n";
                std::cout << "ERROR mano rechazada: corrige e ingresa nuevamente\n";
                return true;
            }

            const std::string key = c->faceKey();
            const int nextCopies = ++addedCounts[key];
            auto it = seenBase.find(key);
            const int usedBase = (it == seenBase.end()) ? 0 : it->second;
            if (usedBase + nextCopies > maxCopiesForCard(st.cfg, *c)) {
                std::cout << "ERROR excede copias disponibles: " << c->faceKey()
                          << " (max=" << maxCopiesForCard(st.cfg, *c) << ")\n";
                std::cout << "ERROR mano rechazada: corrige e ingresa nuevamente\n";
                return true;
            }

            nextHand.push_back(*c);
        }

        for (const Card& c : st.myHand) unmarkSeen(st, c);
        st.myHand = std::move(nextHand);
        for (const Card& c : st.myHand) {
            markSeen(st, c);
        }
        std::cout << "OK mano " << st.myHand.size() << " cartas\n";
        return true;
    }

    if (cmd == "recibo") {
        if (tk.size() < 2) {
            std::cout << "ERROR falta carta\n";
            return true;
        }
        auto c = parseCard(tk[1]);
        if (!c.has_value()) {
            std::cout << "ERROR carta invalida\n";
            return true;
        }
        if (!tryMarkSeen(st, *c)) {
            std::cout << "ERROR excede copias disponibles: " << c->faceKey()
                      << " (max=" << maxCopiesForCard(st.cfg, *c) << ")\n";
            return true;
        }
        st.myHand.push_back(*c);
        std::cout << "OK recibo " << c->toString() << "\n";
        return true;
    }

    if (cmd == "tomo_descarte" || cmd == "toma_descarte" || cmd == "robo_descarte") {
        if (st.discardPile.empty()) {
            std::cout << "ERROR descarte vacio\n";
            return true;
        }

        Card top = st.discardPile.back();
        st.discardPile.pop_back();
        st.myHand.push_back(top);
        std::cout << "OK tomo_descarte " << top.toString() << "\n";
        return true;
    }

    if (cmd == "descarto") {
        if (tk.size() < 2) {
            std::cout << "ERROR falta carta\n";
            return true;
        }
        auto c = parseCard(tk[1]);
        if (!c.has_value()) {
            std::cout << "ERROR carta invalida\n";
            return true;
        }
        if (!removeOneByFace(st.myHand, *c)) {
            std::cout << "ERROR no estaba en mano\n";
            return true;
        }
        st.discardPile.push_back(*c);
        std::cout << "OK descarto " << c->toString() << "\n";
        return true;
    }

    if (cmd == "descarte") {
        if (tk.size() < 2) {
            std::cout << "ERROR falta carta\n";
            return true;
        }
        auto c = parseCard(tk[1]);
        if (!c.has_value()) {
            std::cout << "ERROR carta invalida\n";
            return true;
        }
        if (!tryMarkSeen(st, *c)) {
            std::cout << "ERROR excede copias disponibles: " << c->faceKey()
                      << " (max=" << maxCopiesForCard(st.cfg, *c) << ")\n";
            return true;
        }
        st.discardPile.push_back(*c);
        std::cout << "OK tope descarte " << c->toString() << "\n";
        return true;
    }

    if (cmd == "sale") {
        if (tk.size() < 2) {
            std::cout << "ERROR falta carta\n";
            return true;
        }
        auto c = parseCard(tk[1]);
        if (!c.has_value()) {
            std::cout << "ERROR carta invalida\n";
            return true;
        }
        if (!tryMarkSeen(st, *c)) {
            std::cout << "ERROR excede copias disponibles: " << c->faceKey()
                      << " (max=" << maxCopiesForCard(st.cfg, *c) << ")\n";
            return true;
        }
        std::cout << "OK vista " << c->toString() << "\n";
        return true;
    }

    if (cmd == "bajo" || cmd == "mesa" || cmd == "otro_baja") {
        if (tk.size() < 4) {
            std::cout << "ERROR un juego necesita al menos 3 cartas\n";
            return true;
        }

        size_t startPos = 1;
        if (cmd == "otro_baja") {
            if (tk.size() < 5) {
                std::cout << "ERROR formato: otro_baja <jugadorN> <c1> <c2> <c3> [...]\n";
                return true;
            }
            startPos = 2;
        }

        std::vector<Card> meld;
        bool fail = false;
        for (size_t i = startPos; i < tk.size(); ++i) {
            auto c = parseCard(tk[i]);
            if (!c.has_value()) {
                std::cout << "ERROR carta invalida: " << tk[i] << "\n";
                fail = true;
                break;
            }
            meld.push_back(*c);
        }
        if (fail) return true;

        if (cmd == "bajo") {
            if (!isValidMeld(meld, st.cfg)) {
                std::cout << "ERROR juego no valido con reglas actuales\n";
                return true;
            }

            std::vector<Card> nextHand = st.myHand;
            for (const Card& c : meld) {
                if (!removeOneByFace(nextHand, c)) {
                    std::cout << "ERROR esa carta no esta en mano: " << c.faceKey() << "\n";
                    return true;
                }
            }

            st.myHand = std::move(nextHand);
            for (const Card& c : meld) {
                unmarkSeen(st, c);
            }

            st.tableMelds.push_back(meld);
            std::cout << "OK juego agregado\n";
            return true;
        }

        if (cmd != "bajo") {
            std::unordered_map<std::string, int> addCounts;
            for (const Card& c : meld) {
                const std::string key = c.faceKey();
                const int nextCopies = ++addCounts[key];
                if (!canMarkSeen(st, c, nextCopies)) {
                    std::cout << "ERROR excede copias disponibles: " << c.faceKey()
                              << " (max=" << maxCopiesForCard(st.cfg, c) << ")\n";
                    return true;
                }
            }
        }

        st.tableMelds.push_back(meld);
        for (const Card& c : meld) {
            markSeen(st, c);
        }
        std::cout << "OK juego agregado\n";
        return true;
    }

    if (cmd == "cuelo" || cmd == "otro_cuela") {
        // Format: cuelo <card> en <cards_del_juego>
        if (tk.size() < 4) {
            std::cout << "ERROR formato: cuelo <carta> en <cartas_del_juego>\n";
            return true;
        }

        bool isOther = (cmd == "otro_cuela");
        size_t firstToken = 1;
        if (isOther) {
            if (tk.size() < 5) {
                std::cout << "ERROR formato: otro_cuela <jugadorN> <carta> en <cartas_del_juego>\n";
                return true;
            }
            firstToken = 2;
        }

        size_t enPos = tk.size();
        for (size_t i = firstToken; i < tk.size(); ++i) {
            if (lowerCopy(tk[i]) == "en") {
                enPos = i;
                break;
            }
        }
        if (enPos == tk.size()) {
            std::cout << "ERROR falta palabra 'en'\n";
            return true;
        }

        size_t cardPos = firstToken;
        if (cardPos < tk.size() && (lowerCopy(tk[cardPos]) == "el" || lowerCopy(tk[cardPos]) == "la")) {
            ++cardPos;
        }
        if (cardPos >= enPos) {
            std::cout << "ERROR falta carta a colar\n";
            return true;
        }

        auto c = parseCard(tk[cardPos]);
        if (!c.has_value()) {
            std::cout << "ERROR carta invalida\n";
            return true;
        }

        if (isOther && !canMarkSeen(st, *c)) {
            std::cout << "ERROR excede copias disponibles: " << c->faceKey()
                      << " (max=" << maxCopiesForCard(st.cfg, *c) << ")\n";
            return true;
        }

        std::vector<Card> target;
        for (size_t i = enPos + 1; i < tk.size(); ++i) {
            auto m = parseCard(tk[i]);
            if (m.has_value()) target.push_back(*m);
        }

        if (target.empty()) {
            std::cout << "ERROR falta juego objetivo\n";
            return true;
        }

        std::vector<Card> nextHand = st.myHand;
        if (!isOther) {
            if (!removeOneByFace(nextHand, *c)) {
                std::cout << "ERROR esa carta no esta en mano\n";
                return true;
            }
        }

        bool placed = false;
        for (auto& meld : st.tableMelds) {
            if (meld.size() != target.size()) continue;
            std::multiset<std::string> a, b;
            for (const Card& x : meld) a.insert(x.faceKey());
            for (const Card& x : target) b.insert(x.faceKey());
            if (a == b && canLayoffCardToMeld(*c, meld, st.cfg)) {
                meld.push_back(*c);
                placed = true;
                break;
            }
        }

        if (!placed) {
            std::cout << "ERROR no se pudo colar en el juego indicado\n";
            return true;
        }

        if (!isOther) {
            st.myHand = std::move(nextHand);
        }

        if (isOther) markSeen(st, *c);

        std::cout << "OK carta colada\n";
        return true;
    }

    if (cmd == "muevo" || cmd == "reacomodo") {
        if (tk.size() < 6) {
            std::cout << "ERROR formato: muevo <carta> de Mx a My\n";
            return true;
        }

        auto card = parseCard(tk[1]);
        if (!card.has_value()) {
            std::cout << "ERROR carta invalida\n";
            return true;
        }

        if (lowerCopy(tk[2]) != "de" || lowerCopy(tk[4]) != "a") {
            std::cout << "ERROR formato: muevo <carta> de Mx a My\n";
            return true;
        }

        auto sourceIdx = parseMeldIndexToken(tk[3]);
        auto destIdx = parseMeldIndexToken(tk[5]);
        if (!sourceIdx.has_value() || !destIdx.has_value()) {
            std::cout << "ERROR formato: muevo <carta> de Mx a My\n";
            return true;
        }
        if (*sourceIdx >= st.tableMelds.size() || *destIdx >= st.tableMelds.size() || *sourceIdx == *destIdx) {
            std::cout << "ERROR juegos invalidos\n";
            return true;
        }

        std::vector<Card> sourceOut;
        std::vector<Card> destOut;
        if (!moveCardBetweenMelds(st.tableMelds[*sourceIdx], st.tableMelds[*destIdx], *card, st.cfg, sourceOut, destOut)) {
            std::cout << "ERROR no se pudo mover la carta\n";
            return true;
        }

        st.tableMelds[*sourceIdx] = std::move(sourceOut);
        st.tableMelds[*destIdx] = std::move(destOut);
        std::cout << "OK carta movida\n";
        return true;
    }

    std::cout << "ERROR comando no reconocido. Usa help.\n";
    return false;
}

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    std::cout.setf(std::ios::unitbuf);

    std::string configPath = "rummy.cfg";
    bool useConfigFile = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-c" || arg == "--config") {
            if (i + 1 >= argc) {
                std::cerr << "ERROR falta ruta para " << arg << "\n";
                return 1;
            }
            configPath = argv[++i];
            useConfigFile = true;
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Uso: ./rummy-cli [--config <archivo>]\n";
            printHelp();
            return 0;
        } else {
            std::cerr << "ERROR opcion desconocida: " << arg << "\n";
            std::cerr << "Uso: ./rummy-cli [--config <archivo>]\n";
            return 1;
        }
    }

    GameState st;

    std::cout << "Rummy CLI EV backend listo. Escribi 'help' para comandos.\n";

    if (useConfigFile) {
        std::ifstream cfg(configPath);
        if (!cfg.good()) {
            std::cerr << "ERROR no se pudo abrir archivo de configuracion: " << configPath << "\n";
            return 1;
        } else {
            std::string cfgLine;
            bool shouldExit = false;
            while (std::getline(cfg, cfgLine)) {
                processCommandLine(st, cfgLine, shouldExit);
                if (shouldExit) return 0;
            }
        }
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        bool shouldExit = false;
        processCommandLine(st, line, shouldExit);
        if (shouldExit) break;
    }

    return 0;
}
