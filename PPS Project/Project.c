/*
 * Project 58: Stock Market Portfolio Simulator
 * ---------------------------------------------------------------
 * Features:
 *   - Buy / sell stocks (weighted-average buy price on repeat buys)
 *   - View holdings with per-stock and total profit/loss
 *   - Update a stock's current price manually or simulate a market move
 *   - Portfolio value, invested amount, unrealized + realized P/L,
 *     return %, best / worst performer
 *   - Dynamic array of structures (malloc / realloc / free)
 *   - Save / load portfolio to a text file
 *
 * Compile:  gcc portfolio_simulator.c -o portfolio -Wall -Wextra
 * Run:      ./portfolio
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#define SYMBOL_LEN     16
#define INITIAL_CAP    4
#define DATA_FILE      "portfolio.txt"
#define STARTING_CASH  100000.0

/* ---------- Data structures ---------- */

typedef struct {
    char   symbol[SYMBOL_LEN];
    int    quantity;
    double buy_price;       /* average buy price per share */
    double current_price;   /* latest market price per share */
} Stock;

typedef struct {
    Stock *holdings;        /* dynamic array of Stock */
    int    count;           /* number of stocks held */
    int    capacity;        /* allocated slots */
    double cash;            /* available cash */
    double realized_pl;     /* profit/loss booked from sales */
} Portfolio;

/* ---------- Input helpers ---------- */

static void read_line(const char *prompt, char *buf, size_t size)
{
    printf("%s", prompt);
    if (fgets(buf, (int)size, stdin) == NULL) {
        buf[0] = '\0';
        return;
    }
    buf[strcspn(buf, "\n")] = '\0';
}

static int read_int(const char *prompt)
{
    char line[64], *end;
    long v;
    for (;;) {
        read_line(prompt, line, sizeof line);
        v = strtol(line, &end, 10);
        if (end != line && *end == '\0')
            return (int)v;
        printf("  Invalid number, try again.\n");
    }
}

static double read_double(const char *prompt)
{
    char line[64], *end;
    double v;
    for (;;) {
        read_line(prompt, line, sizeof line);
        v = strtod(line, &end);
        if (end != line && *end == '\0')
            return v;
        printf("  Invalid number, try again.\n");
    }
}

static void read_symbol(char *out)
{
    char line[64];
    size_t i;
    read_line("Enter stock symbol: ", line, sizeof line);
    strncpy(out, line, SYMBOL_LEN - 1);
    out[SYMBOL_LEN - 1] = '\0';
    for (i = 0; out[i]; i++)
        out[i] = (char)toupper((unsigned char)out[i]);
}

/* ---------- Portfolio memory management ---------- */

static int init_portfolio(Portfolio *p, double cash)
{
    p->holdings = (Stock *)malloc(INITIAL_CAP * sizeof(Stock));
    if (p->holdings == NULL) {
        printf("Memory allocation failed!\n");
        return 0;
    }
    p->count = 0;
    p->capacity = INITIAL_CAP;
    p->cash = cash;
    p->realized_pl = 0.0;
    return 1;
}

static void free_portfolio(Portfolio *p)
{
    free(p->holdings);
    p->holdings = NULL;
    p->count = p->capacity = 0;
}

static int grow_if_full(Portfolio *p)
{
    Stock *tmp;
    if (p->count < p->capacity)
        return 1;
    tmp = (Stock *)realloc(p->holdings, (size_t)p->capacity * 2 * sizeof(Stock));
    if (tmp == NULL) {
        printf("Memory reallocation failed!\n");
        return 0;
    }
    p->holdings = tmp;
    p->capacity *= 2;
    return 1;
}

/* Returns pointer to the stock, or NULL if not held. */
static Stock *find_stock(Portfolio *p, const char *symbol)
{
    Stock *s;
    for (s = p->holdings; s < p->holdings + p->count; s++)
        if (strcmp(s->symbol, symbol) == 0)
            return s;
    return NULL;
}

/* ---------- Calculation functions ---------- */

static double stock_cost(const Stock *s)   { return s->quantity * s->buy_price; }
static double stock_value(const Stock *s)  { return s->quantity * s->current_price; }
static double stock_pl(const Stock *s)     { return stock_value(s) - stock_cost(s); }

static double stock_pl_percent(const Stock *s)
{
    double cost = stock_cost(s);
    return cost > 0 ? (stock_pl(s) / cost) * 100.0 : 0.0;
}

static double total_invested(const Portfolio *p)
{
    double sum = 0.0;
    int i;
    for (i = 0; i < p->count; i++)
        sum += stock_cost(&p->holdings[i]);
    return sum;
}

static double total_market_value(const Portfolio *p)
{
    double sum = 0.0;
    int i;
    for (i = 0; i < p->count; i++)
        sum += stock_value(&p->holdings[i]);
    return sum;
}

static double total_unrealized_pl(const Portfolio *p)
{
    return total_market_value(p) - total_invested(p);
}

/* ---------- Core operations ---------- */

static void buy_stock(Portfolio *p)
{
    char symbol[SYMBOL_LEN];
    int qty;
    double price, cost;
    Stock *s;

    read_symbol(symbol);
    if (symbol[0] == '\0') {
        printf("Symbol cannot be empty.\n");
        return;
    }
    qty = read_int("Quantity to buy: ");
    price = read_double("Buy price per share: ");

    if (qty <= 0 || price <= 0) {
        printf("Quantity and price must be positive.\n");
        return;
    }
    cost = qty * price;
    if (cost > p->cash) {
        printf("Insufficient cash! Need %.2f, have %.2f.\n", cost, p->cash);
        return;
    }

    s = find_stock(p, symbol);
    if (s != NULL) {
        /* weighted average buy price */
        double total_cost = stock_cost(s) + cost;
        s->quantity += qty;
        s->buy_price = total_cost / s->quantity;
        s->current_price = price;
    } else {
        if (!grow_if_full(p))
            return;
        s = &p->holdings[p->count++];
        strcpy(s->symbol, symbol);
        s->quantity = qty;
        s->buy_price = price;
        s->current_price = price;
    }
    p->cash -= cost;
    printf("Bought %d share(s) of %s at %.2f. Cash left: %.2f\n",
           qty, symbol, price, p->cash);
}

static void sell_stock(Portfolio *p)
{
    char symbol[SYMBOL_LEN];
    int qty;
    double price, proceeds, profit;
    Stock *s;

    read_symbol(symbol);
    s = find_stock(p, symbol);
    if (s == NULL) {
        printf("You don't hold %s.\n", symbol);
        return;
    }
    printf("You hold %d share(s) of %s (avg buy %.2f, current %.2f).\n",
           s->quantity, s->symbol, s->buy_price, s->current_price);

    qty = read_int("Quantity to sell: ");
    if (qty <= 0 || qty > s->quantity) {
        printf("Invalid quantity.\n");
        return;
    }
    price = read_double("Sell price per share: ");
    if (price <= 0) {
        printf("Price must be positive.\n");
        return;
    }

    proceeds = qty * price;
    profit = qty * (price - s->buy_price);
    p->cash += proceeds;
    p->realized_pl += profit;
    s->quantity -= qty;
    s->current_price = price;

    printf("Sold %d share(s) of %s at %.2f. %s: %.2f\n", qty, symbol, price,
           profit >= 0 ? "Profit" : "Loss", profit >= 0 ? profit : -profit);

    if (s->quantity == 0) {
        /* remove by shifting the remaining elements left */
        Stock *end = p->holdings + p->count;
        Stock *q;
        for (q = s; q + 1 < end; q++)
            *q = *(q + 1);
        p->count--;
        printf("Position in %s closed.\n", symbol);
    }
}

static void update_price(Portfolio *p)
{
    char symbol[SYMBOL_LEN];
    double price;
    Stock *s;

    read_symbol(symbol);
    s = find_stock(p, symbol);
    if (s == NULL) {
        printf("You don't hold %s.\n", symbol);
        return;
    }
    price = read_double("New current price: ");
    if (price <= 0) {
        printf("Price must be positive.\n");
        return;
    }
    s->current_price = price;
    printf("Price of %s updated to %.2f\n", symbol, price);
}

/* Random daily move of -5% .. +5% for every holding */
static void simulate_market(Portfolio *p)
{
    int i;
    if (p->count == 0) {
        printf("No holdings to simulate.\n");
        return;
    }
    for (i = 0; i < p->count; i++) {
        double change = ((rand() % 1001) / 100.0) - 5.0;   /* -5.00 .. +5.00 */
        p->holdings[i].current_price *= (1.0 + change / 100.0);
        if (p->holdings[i].current_price < 0.01)
            p->holdings[i].current_price = 0.01;
        printf("  %-8s %+6.2f%%  -> %.2f\n", p->holdings[i].symbol,
               change, p->holdings[i].current_price);
    }
    printf("Market simulated for one day.\n");
}

/* ---------- Display ---------- */

static void view_holdings(const Portfolio *p)
{
    int i;
    if (p->count == 0) {
        printf("\nNo holdings.\n");
        return;
    }
    printf("\n%-8s %6s %10s %10s %12s %12s %8s\n",
           "SYMBOL", "QTY", "BUY", "CURRENT", "VALUE", "P/L", "P/L %");
    printf("---------------------------------------------------------------------------\n");
    for (i = 0; i < p->count; i++) {
        const Stock *s = &p->holdings[i];
        printf("%-8s %6d %10.2f %10.2f %12.2f %12.2f %7.2f%%\n",
               s->symbol, s->quantity, s->buy_price, s->current_price,
               stock_value(s), stock_pl(s), stock_pl_percent(s));
    }
    printf("---------------------------------------------------------------------------\n");
}

static void show_summary(const Portfolio *p)
{
    double invested = total_invested(p);
    double market = total_market_value(p);
    double unreal = total_unrealized_pl(p);
    double ret = invested > 0 ? (unreal / invested) * 100.0 : 0.0;
    double net_worth = p->cash + market;
    int i, best = -1, worst = -1;

    view_holdings(p);

    printf("\n=========== PORTFOLIO SUMMARY ===========\n");
    printf("Cash available      : %12.2f\n", p->cash);
    printf("Amount invested     : %12.2f\n", invested);
    printf("Current market value: %12.2f\n", market);
    printf("Unrealized P/L      : %12.2f (%.2f%%)\n", unreal, ret);
    printf("Realized P/L        : %12.2f\n", p->realized_pl);
    printf("Total P/L           : %12.2f\n", unreal + p->realized_pl);
    printf("Net worth           : %12.2f\n", net_worth);
    printf("Overall vs start    : %12.2f (%.2f%%)\n",
           net_worth - STARTING_CASH,
           ((net_worth - STARTING_CASH) / STARTING_CASH) * 100.0);

    for (i = 0; i < p->count; i++) {
        if (best < 0 || stock_pl_percent(&p->holdings[i]) > stock_pl_percent(&p->holdings[best]))
            best = i;
        if (worst < 0 || stock_pl_percent(&p->holdings[i]) < stock_pl_percent(&p->holdings[worst]))
            worst = i;
    }
    if (best >= 0) {
        printf("Best performer      : %s (%.2f%%)\n",
               p->holdings[best].symbol, stock_pl_percent(&p->holdings[best]));
        printf("Worst performer     : %s (%.2f%%)\n",
               p->holdings[worst].symbol, stock_pl_percent(&p->holdings[worst]));
    }
    printf("=========================================\n");
}

/* ---------- File persistence ---------- */

static int save_portfolio(const Portfolio *p, const char *filename)
{
    FILE *fp = fopen(filename, "w");
    int i;
    if (fp == NULL) {
        printf("Could not open %s for writing.\n", filename);
        return 0;
    }
    fprintf(fp, "%.2f %.2f %d\n", p->cash, p->realized_pl, p->count);
    for (i = 0; i < p->count; i++)
        fprintf(fp, "%s %d %.2f %.2f\n", p->holdings[i].symbol,
                p->holdings[i].quantity, p->holdings[i].buy_price,
                p->holdings[i].current_price);
    fclose(fp);
    return 1;
}

static int load_portfolio(Portfolio *p, const char *filename)
{
    FILE *fp = fopen(filename, "r");
    int n, i;
    double cash, realized;

    if (fp == NULL)
        return 0;
    if (fscanf(fp, "%lf %lf %d", &cash, &realized, &n) != 3 || n < 0) {
        fclose(fp);
        return 0;
    }

    p->count = 0;
    p->cash = cash;
    p->realized_pl = realized;

    for (i = 0; i < n; i++) {
        Stock s;
        if (fscanf(fp, "%15s %d %lf %lf", s.symbol, &s.quantity,
                   &s.buy_price, &s.current_price) != 4)
            break;
        if (!grow_if_full(p))
            break;
        p->holdings[p->count++] = s;
    }
    fclose(fp);
    return 1;
}

/* ---------- Menu ---------- */

static void print_menu(void)
{
    printf("\n====== STOCK PORTFOLIO SIMULATOR ======\n");
    printf(" 1. Buy stock\n");
    printf(" 2. Sell stock\n");
    printf(" 3. View holdings\n");
    printf(" 4. Update stock price\n");
    printf(" 5. Simulate market (random daily move)\n");
    printf(" 6. Portfolio summary & performance\n");
    printf(" 7. Save portfolio to file\n");
    printf(" 8. Load portfolio from file\n");
    printf(" 0. Exit\n");
    printf("=======================================\n");
}

int main(void)
{
    Portfolio portfolio;
    int choice;

    srand((unsigned)time(NULL));

    if (!init_portfolio(&portfolio, STARTING_CASH))
        return 1;

    if (load_portfolio(&portfolio, DATA_FILE))
        printf("Previous portfolio loaded from %s.\n", DATA_FILE);
    else
        printf("Starting new portfolio with cash %.2f.\n", STARTING_CASH);

    do {
        print_menu();
        choice = read_int("Enter your choice: ");

        switch (choice) {
        case 1: buy_stock(&portfolio); break;
        case 2: sell_stock(&portfolio); break;
        case 3: view_holdings(&portfolio); break;
        case 4: update_price(&portfolio); break;
        case 5: simulate_market(&portfolio); break;
        case 6: show_summary(&portfolio); break;
        case 7:
            if (save_portfolio(&portfolio, DATA_FILE))
                printf("Portfolio saved to %s.\n", DATA_FILE);
            break;
        case 8:
            if (load_portfolio(&portfolio, DATA_FILE))
                printf("Portfolio loaded from %s.\n", DATA_FILE);
            else
                printf("Could not load %s.\n", DATA_FILE);
            break;
        case 0:
            if (save_portfolio(&portfolio, DATA_FILE))
                printf("Portfolio auto-saved to %s.\n", DATA_FILE);
            printf("Goodbye!\n");
            break;
        default:
            printf("Invalid choice, try again.\n");
        }
    } while (choice != 0);

    free_portfolio(&portfolio);
    return 0;
}