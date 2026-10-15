#include "simlib.h"

/*
 * SIMLIB model of the rental-car shuttle. Times are in minutes
 * (80 hours = 4,800 minutes).
 *
 * Events:
 *   A1/A2/A3     a person arrives at location 1/2/3
 *   BUS_ARRIVE   bus reaches a stop and starts unloading
 *   UNLOAD_DONE  one passenger has finished getting off
 *   LOAD_DONE    one passenger has finished getting on
 *   MIN_DWELL    the bus has been at the stop for the minimum time
 *   BUS_LEAVE    bus starts driving to the next stop
 *   END_RUN      end of the simulation
 *
 * At each stop the bus unloads one passenger at a time, then loads one at a
 * time from the FIFO queue. It leaves once the minimum dwell time is over
 * and nobody else can board (bus full or queue empty). People who arrive
 * while the bus is waiting at their stop board.
 */

#define A1              1
#define A2              2
#define A3              3
#define BUS_ARRIVE      4
#define BUS_LEAVE       5
#define UNLOAD_DONE     6
#define LOAD_DONE       7
#define MIN_DWELL       8
#define END_RUN         9

#define Q1              1
#define Q2              2
#define Q3              3

#define HORIZON         4800.0
#define CAPACITY        20
#define MIN_DWELL_TIME  5.0

#define STREAM_UNLOAD   4
#define STREAM_LOAD     5
#define STREAM_DEST     6

/* Stream s is seeded with SEED_BASE + SEED_STEP * s. */
#define SEED_BASE       123456789L
#define SEED_STEP       97L

#define TRAVELING       0
#define UNLOADING       1
#define LOADING         2
#define WAITING         3   /* at a stop, nobody boarding, minimum dwell not over */

typedef struct
{
    int destination, origin;
    double arrival;
} Passenger;

static Passenger onboard[CAPACITY];

static int n_onboard, location = 3, queue_id[3] = {Q1, Q2, Q3};
static int max_queue[3], max_onboard, departures3;

static double arrivals[3], q_area[3], q_clock[3];
static double delay_total[3], delay_max[3], delay_count[3];
static double bus_area, bus_clock, person_total, person_min, person_max, people_done;
static double stop_total[3], stop_min[3], stop_max[3], stop_count[3];
static double loop_total, loop_min, loop_max, last_departure3;
static double trip_total[3], trip_min[3], trip_max[3], trip_count[3];
static double stop_start;

static int bus_state = TRAVELING, min_dwell_over, next_location;
static FILE *out;

static void arrival(int loc);
static void bus_arrival(void);
static void unload_next(void);
static void unload_done(void);
static void load_next(void);
static void load_done(void);
static void min_dwell(void);
static void depart(void);
static void bus_leave(void);

int main(void)
{
    int event;

    out = fopen("car_rental.out", "w");
    if (!out) return 1;

    init_simlib();
    maxatr = 4;
    for (int s = 1; s <= 6; s++) lcgrandst(SEED_BASE + SEED_STEP * s, s);

    /* Mean interarrival time in minutes is 60 / (people per hour). */
    event_schedule(expon(60.0 / 14.0, 1), A1);
    event_schedule(expon(60.0 / 10.0, 2), A2);
    event_schedule(expon(60.0 / 24.0, 3), A3);

    event_schedule(0.0, BUS_LEAVE);
    event_schedule(HORIZON, END_RUN);

    while (1) {
        timing();
        event = next_event_type;

        if (event == END_RUN) break;

        if      (event == A1)          arrival(1);
        else if (event == A2)          arrival(2);
        else if (event == A3)          arrival(3);
        else if (event == BUS_ARRIVE)  bus_arrival();
        else if (event == UNLOAD_DONE) unload_done();
        else if (event == LOAD_DONE)   load_done();
        else if (event == MIN_DWELL)   min_dwell();
        else if (event == BUS_LEAVE)   bus_leave();
    }

    /* Close the time averages at the end of the run. */
    bus_area += n_onboard * (HORIZON - bus_clock);
    for (int i = 0; i < 3; i++)
        q_area[i] += list_size[queue_id[i]] * (HORIZON - q_clock[i]);

    fprintf(out, "CAR-RENTAL SHUTTLE - SIMLIB / C - 80 hours\n");
    fprintf(out, "Seeds (stream 1-6, base %ld + %ld * stream):", SEED_BASE, SEED_STEP);
    for (int s = 1; s <= 6; s++) fprintf(out, " %ld", SEED_BASE + SEED_STEP * s);
    fprintf(out, "\n");

    fprintf(out, "\n(a) QUEUE LENGTH\nLocation  Arrivals  Average number  Maximum number\n");
    for (int i = 0; i < 3; i++)
        fprintf(out, "   %d       %5.0f       %8.3f          %d\n",
                i + 1,
                arrivals[i],
                q_area[i] / HORIZON,
                max_queue[i]);

    fprintf(out, "\n(b) QUEUE DELAY (minutes)\nLocation  Average  Maximum\n");
    for (int i = 0; i < 3; i++)
        fprintf(out, "   %d       %7.3f  %7.3f\n",
                i + 1,
                delay_count[i] ? delay_total[i] / delay_count[i] : 0.0,
                delay_max[i]);

    fprintf(out, "\n(c) BUS LOAD\nAverage: %.3f people | Maximum: %d people\n",
            bus_area / HORIZON,
            max_onboard);

    fprintf(out, "\n(d) STOP TIME BY LOCATION (minutes)\nLocation  Average  Minimum  Maximum\n");
    for (int i = 0; i < 3; i++)
        fprintf(out, "   %d       %7.3f  %7.3f  %7.3f\n",
                i + 1,
                stop_count[i] ? stop_total[i] / stop_count[i] : 0.0,
                stop_count[i] ? stop_min[i] : 0.0,
                stop_max[i]);

    fprintf(out, "\n(e) LOOP: departure from rental to next departure (minutes)\nAverage %.3f | Minimum %.3f | Maximum %.3f\n",
            departures3 ? loop_total / departures3 : 0.0,
            departures3 ? loop_min : 0.0,
            loop_max);

    fprintf(out, "\n(f) PERSON TIME IN SYSTEM BY ARRIVAL LOCATION (minutes)\nLocation  Average  Minimum  Maximum  Completed\n");
    for (int i = 0; i < 3; i++)
        fprintf(out, "   %d       %7.3f  %7.3f  %7.3f    %6.0f\n",
                i + 1,
                trip_count[i] ? trip_total[i] / trip_count[i] : 0.0,
                trip_min[i],
                trip_max[i],
                trip_count[i]);

    fprintf(out, "Overall: average %.3f | minimum %.3f | maximum %.3f | completed trips %.0f\n",
            people_done ? person_total / people_done : 0.0,
            person_min,
            person_max,
            people_done);

    fclose(out);
    return 0;
}

static void arrival(int loc)
{
    int q = queue_id[loc - 1];
    double rate[3] = {14.0, 10.0, 24.0};

    event_schedule(sim_time + expon(60.0 / rate[loc - 1], loc), loc);

    q_area[loc - 1] += list_size[q] * (sim_time - q_clock[loc - 1]);
    q_clock[loc - 1] = sim_time;

    transfer[1] = sim_time;
    list_file(LAST, q);
    arrivals[loc - 1]++;

    q_area[loc - 1] += list_size[q] * (sim_time - q_clock[loc - 1]);
    q_clock[loc - 1] = sim_time;

    if (list_size[q] > max_queue[loc - 1]) max_queue[loc - 1] = list_size[q];

    if (bus_state == WAITING && location == loc) load_next();
}

static void bus_leave(void)
{
    /* Route: rental (3) -> terminal 1 -> terminal 2 -> rental (3), with legs
       of 4.5, 1 and 4.5 miles at 30 miles/hour. */
    double miles = (location == 3) ? 4.5 : ((location == 1) ? 1.0 : 4.5);
    next_location = (location == 3) ? 1 : ((location == 1) ? 2 : 3);

    bus_state = TRAVELING;
    event_schedule(sim_time + miles * 60.0 / 30.0, BUS_ARRIVE);
}

static void bus_arrival(void)
{
    bus_area += n_onboard * (sim_time - bus_clock);
    bus_clock = sim_time;

    location = next_location;
    stop_start = sim_time;
    min_dwell_over = 0;

    event_schedule(sim_time + MIN_DWELL_TIME, MIN_DWELL);

    unload_next();
}

/* Unload the next passenger bound for this stop; start loading when none are left. */
static void unload_next(void)
{
    for (int i = 0; i < n_onboard; i++) {
        if (onboard[i].destination == location) {
            bus_state = UNLOADING;
            event_schedule(sim_time + uniform(16.0 / 60.0, 24.0 / 60.0, STREAM_UNLOAD),
                           UNLOAD_DONE);
            return;
        }
    }

    load_next();
}

static void unload_done(void)
{
    int i = 0;
    double trip;

    /* The passenger just unloaded is the first one bound for this stop. */
    while (onboard[i].destination != location) i++;

    trip = sim_time - onboard[i].arrival;

    if (people_done == 0 || trip < person_min) person_min = trip;
    if (trip_count[onboard[i].origin - 1] == 0 || trip < trip_min[onboard[i].origin - 1])
        trip_min[onboard[i].origin - 1] = trip;

    person_total += trip;
    people_done++;

    trip_total[onboard[i].origin - 1] += trip;
    trip_count[onboard[i].origin - 1]++;

    if (trip > person_max) person_max = trip;
    if (trip > trip_max[onboard[i].origin - 1]) trip_max[onboard[i].origin - 1] = trip;

    bus_area += n_onboard * (sim_time - bus_clock);
    bus_clock = sim_time;

    for (; i < n_onboard - 1; i++) onboard[i] = onboard[i + 1];
    n_onboard--;

    unload_next();
}

/* Board the next person in the queue if there is room; otherwise leave,
   or wait if the minimum dwell time is not over. */
static void load_next(void)
{
    int q = queue_id[location - 1];

    if (n_onboard < CAPACITY && list_size[q] > 0) {
        q_area[location - 1] += list_size[q] * (sim_time - q_clock[location - 1]);
        q_clock[location - 1] = sim_time;

        list_remove(FIRST, q);

        q_area[location - 1] += list_size[q] * (sim_time - q_clock[location - 1]);
        q_clock[location - 1] = sim_time;

        double delay = sim_time - transfer[1];

        delay_total[location - 1] += delay;
        delay_count[location - 1]++;
        if (delay > delay_max[location - 1]) delay_max[location - 1] = delay;

        /* The person takes a seat as soon as boarding starts. */
        bus_area += n_onboard * (sim_time - bus_clock);
        bus_clock = sim_time;

        onboard[n_onboard].arrival = transfer[1];
        onboard[n_onboard].origin = location;

        /* Terminal passengers go to rental; rental passengers go to terminal 1 with probability 0.583. */
        onboard[n_onboard].destination =
            (location == 3) ? ((uniform(0.0, 1.0, STREAM_DEST) < 0.583) ? 1 : 2) : 3;

        n_onboard++;
        if (n_onboard > max_onboard) max_onboard = n_onboard;

        bus_state = LOADING;
        event_schedule(sim_time + uniform(15.0 / 60.0, 25.0 / 60.0, STREAM_LOAD),
                       LOAD_DONE);
    }
    else if (min_dwell_over)
        depart();
    else
        bus_state = WAITING;
}

static void load_done(void)
{
    load_next();
}

static void min_dwell(void)
{
    min_dwell_over = 1;

    if (bus_state == WAITING) depart();
}

static void depart(void)
{
    double dwell = sim_time - stop_start;

    stop_total[location - 1] += dwell;
    stop_count[location - 1]++;
    if (stop_min[location - 1] == 0.0 || dwell < stop_min[location - 1]) stop_min[location - 1] = dwell;
    if (dwell > stop_max[location - 1]) stop_max[location - 1] = dwell;

    /* Loop time runs from one departure at rental to the next. The bus
       leaves rental at t = 0, so last_departure3 = 0 starts the first loop. */
    if (location == 3) {
        double loop = sim_time - last_departure3;

        loop_total += loop;
        departures3++;

        if (departures3 == 1 || loop < loop_min) loop_min = loop;
        if (loop > loop_max) loop_max = loop;

        last_departure3 = sim_time;
    }

    bus_leave();
}
