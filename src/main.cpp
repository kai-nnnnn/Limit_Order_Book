// Command-line driver: reads one command per line from stdin (or a file) and
// prints the resulting trades / book state.
//
//   LIMIT  BUY|SELL <id> <price> <qty>
//   MARKET BUY|SELL <id> <qty>
//   CANCEL <id>
//   PRINT
//   # comment lines and blank lines are ignored
//
// Example:  ./build/lob_cli examples/lesson1.txt

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "orderbook/order_book.hpp"

namespace {

using namespace ob;

bool parse_side(const std::string& token, Side& out) {
    if (token == "BUY" || token == "B") {
        out = Side::Buy;
        return true;
    }
    if (token == "SELL" || token == "S") {
        out = Side::Sell;
        return true;
    }
    return false;
}

const char* to_string(Status s) {
    switch (s) {
        case Status::Accepted: return "ACCEPTED";
        case Status::RejectedDuplicateId: return "REJECTED duplicate id";
        case Status::RejectedZeroQuantity: return "REJECTED zero quantity";
        case Status::RejectedInvalidPrice: return "REJECTED invalid price";
    }
    return "UNKNOWN";
}

void report(OrderId id, const AddResult& r) {
    if (!r.accepted()) {
        std::cout << "order " << id << ": " << to_string(r.status) << '\n';
        return;
    }
    for (const Trade& t : r.trades) {
        std::cout << "TRADE price=" << t.price << " qty=" << t.quantity
                  << " maker=" << t.maker_id << " taker=" << t.taker_id
                  << " (" << ob::to_string(t.taker_side) << " aggressor)\n";
    }
    std::cout << "order " << id << ": filled=" << r.filled << " resting=" << r.resting << '\n';
}

void print_book(const OrderBook& book) {
    const auto asks = book.depth(Side::Sell, 10);
    const auto bids = book.depth(Side::Buy, 10);

    std::cout << "---------- ASK ----------\n";
    // Print asks from highest to lowest so the spread sits in the middle.
    for (auto it = asks.rbegin(); it != asks.rend(); ++it) {
        std::cout << std::setw(10) << it->price << " | " << std::setw(8) << it->total_quantity
                  << "  (" << it->order_count << " orders)\n";
    }
    std::cout << "-------------------------\n";
    for (const auto& lvl : bids) {
        std::cout << std::setw(10) << lvl.price << " | " << std::setw(8) << lvl.total_quantity
                  << "  (" << lvl.order_count << " orders)\n";
    }
    std::cout << "---------- BID ----------\n";
}

void run(std::istream& in) {
    OrderBook book;
    std::string line;
    int line_no = 0;

    while (std::getline(in, line)) {
        ++line_no;
        std::istringstream ss(line);
        std::string cmd;
        if (!(ss >> cmd) || cmd[0] == '#') continue;

        if (cmd == "LIMIT") {
            std::string side_tok;
            OrderId id;
            Price price;
            Quantity qty;
            Side side;
            if (ss >> side_tok >> id >> price >> qty && parse_side(side_tok, side)) {
                report(id, book.add_limit(id, side, price, qty));
                continue;
            }
        } else if (cmd == "MARKET") {
            std::string side_tok;
            OrderId id;
            Quantity qty;
            Side side;
            if (ss >> side_tok >> id >> qty && parse_side(side_tok, side)) {
                report(id, book.add_market(id, side, qty));
                continue;
            }
        } else if (cmd == "CANCEL") {
            OrderId id;
            if (ss >> id) {
                std::cout << "cancel " << id << ": " << (book.cancel(id) ? "OK" : "NOT FOUND")
                          << '\n';
                continue;
            }
        } else if (cmd == "PRINT") {
            print_book(book);
            continue;
        }
        std::cerr << "line " << line_no << ": cannot parse: " << line << '\n';
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1) {
        std::ifstream file(argv[1]);
        if (!file) {
            std::cerr << "cannot open " << argv[1] << '\n';
            return 1;
        }
        run(file);
    } else {
        run(std::cin);
    }
    return 0;
}
