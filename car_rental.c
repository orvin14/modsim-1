#include "simlib.h"

/*
 * SIMLIB model of the rental-car shuttle.
 * All simulation times are in minutes (80 hours = 4,800 minutes).
 *
 * A1/A2/A3 are passenger-arrival events. The bus moves through its route
 * with these events:
 *   BUS_ARRIVE   bus reaches a stop and starts unloading
 *   UNLOAD_DONE  one passenger has finished getting off
 *   LOAD_DONE    one passenger has finished getting on
 *   MIN_DWELL    the bus has been at the stop for the minimum 5 minutes
 *   BUS_LEAVE    bus starts driving to the next stop
 * END_RUN stops the model at 80 hours.
 *
 * At each stop the bus unloads one passenger at a time, then loads one
 * passenger at a time from the FIFO queue. It leaves once the minimum
 * dwell time is over and nobody else can board (bus full or queue empty).
 * People who arrive while the bus is still waiting at their stop board.
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

#define STREAM_DEST     4
#define STREAM_UNLOAD   5
#define STREAM_LOAD     6

/* What the bus is doing right now. */
#define TRAVELING       0
#define UNLOADING       1
#define LOADING         2
#define WAITING         3   /* at a stop, nobody boarding, minimum dwell not over */

/* Information kept for each passenger after boarding the bus. */
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
static double bus_area, bus_clock, person_total, person_max, people_done;
static double stop_total[3], stop_min[3], stop_max[3], stop_count[3];
static double loop_total, loop_min, loop_max, last_departure3;
static double trip_total[3], trip_max[3], trip_count[3];
static double stop_start, onboard_arrival[CAPACITY];

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

    /* Initialize SIMLIB's event/list machinery and fix all random streams. */
    init_simlib();
    maxatr = 4;
    maxlist = 7;
    for (int s = 1; s <= 6; s++) lcgrandst(123456789L + 97L * s, s); /* fixed streams */

    /* Schedule first passenger arrivals. Rates are people/hour, so convert
       their mean interarrival times to minutes before calling expon(). */
    event_schedule(expon(60.0 / 14.0, 1), A1);
    event_schedule(expon(60.0 / 10.0, 2), A2);
    event_schedule(expon(60.0 / 24.0, 3), A3);

    /* The bus starts at the rental location and leaves immediately. */
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

    /* Extend time-weighted queue and bus statistics to the 80-hour cutoff. */
    bus_area += n_onboard * (HORIZON - bus_clock);
    for (int i = 0; i < 3; i++)
        q_area[i] += list_size[queue_id[i]] * (HORIZON - q_clock[i]);

    fprintf(out, "CAR-RENTAL SHUTTLE - SIMLIB / C - 80 hours\nSeed: 123456789\n");

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

    fprintf(out, "\n(f) PERSON TIME IN SYSTEM BY ARRIVAL LOCATION (minutes)\nLocation  Average  Maximum  Completed\n");
    for (int i = 0; i < 3; i++)
        fprintf(out, "   %d       %7.3f  %7.3f    %6.0f\n",
                i + 1,
                trip_count[i] ? trip_total[i] / trip_count[i] : 0.0,
                trip_max[i],
                trip_count[i]);

    fprintf(out, "Overall: average %.3f | maximum %.3f | completed trips %.0f\n",
            people_done ? person_total / people_done : 0.0,
            person_max,
            people_done);

    fclose(out);
    return 0;
}

static void arrival(int loc)
{
    int q = queue_id[loc - 1];
    double rate[3] = {14.0, 10.0, 24.0};

    /* Schedule the next arrival at this location (Poisson process). */
    event_schedule(sim_time + expon(60.0 / rate[loc - 1], loc), loc);

    /* Update queue area, then append this person's arrival time at FIFO tail. */
    q_area[loc - 1] += list_size[q] * (sim_time - q_clock[loc - 1]);
    q_clock[loc - 1] = sim_time;

    transfer[1] = sim_time;
    list_file(LAST, q);
    arrivals[loc - 1]++;

    q_area[loc - 1] += list_size[q] * (sim_time - q_clock[loc - 1]);
    q_clock[loc - 1] = sim_time;

    if (list_size[q] > max_queue[loc - 1]) max_queue[loc - 1] = list_size[q];

    /* A bus that is waiting at this stop picks the new arrival up right away. */
    if (bus_state == WAITING && location == loc) load_next();
}

static void bus_leave(void)
{
    /* Route: rental (3) -> terminal 1 -> terminal 2 -> rental (3).
       Distances are 4.5, 1, and 4.5 miles; speed is 30 miles/hour. */
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

    /* The bus must spend at least five minutes at each location. */
    event_schedule(sim_time + MIN_DWELL_TIME, MIN_DWELL);

    unload_next();
}

/* Start unloading the next passenger bound for this stop (FIFO order).
   When nobody is left to unload, loading begins. */
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

/* The passenger being unloaded has gotten off the bus. */
static void unload_done(void)
{
    int i = 0;
    double trip;

    /* The passenger being unloaded is the first one bound for this stop. */
    while (onboard[i].destination != location) i++;

    trip = sim_time - onboard[i].arrival;

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

/* Start boarding the first person in this stop's queue if the bus has room.
   Otherwise leave now, or wait if the minimum dwell time is not over. */
static void load_next(void)
{
    int q = queue_id[location - 1];

    if (n_onboard < CAPACITY && list_size[q] > 0) {
        q_area[location - 1] += list_size[q] * (sim_time - q_clock[location - 1]);
        q_clock[location - 1] = sim_time;

        list_remove(FIRST, q);

        q_area[location - 1] += list_size[q] * (sim_time - q_clock[location - 1]);
        q_clock[location - 1] = sim_time;

        /* Queue delay: time from joining the queue until boarding starts. */
        double delay = sim_time - transfer[1];

        delay_total[location - 1] += delay;
        delay_count[location - 1]++;
        if (delay > delay_max[location - 1]) delay_max[location - 1] = delay;

        /* The person takes a seat as soon as boarding starts. */
        bus_area += n_onboard * (sim_time - bus_clock);
        bus_clock = sim_time;

        onboard[n_onboard].arrival = transfer[1];
        onboard[n_onboard].origin = location;

        /* Terminal passengers go to rental; rental passengers choose a
           terminal using the probabilities given in the problem statement. */
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

/* The passenger being loaded is seated; look at the queue again. */
static void load_done(void)
{
    load_next();
}

/* Five minutes have passed since the bus arrived. */
static void min_dwell(void)
{
    min_dwell_over = 1;

    if (bus_state == WAITING) depart();
}

/* Nobody else can board and the minimum dwell time is over: record the
   stop and drive on. */
static void depart(void)
{
    double dwell = sim_time - stop_start;

    stop_total[location - 1] += dwell;
    stop_count[location - 1]++;
    if (stop_min[location - 1] == 0.0 || dwell < stop_min[location - 1]) stop_min[location - 1] = dwell;
    if (dwell > stop_max[location - 1]) stop_max[location - 1] = dwell;

    /* A loop is measured from one departure at rental to the next. */
    if (location == 3) {
        if (last_departure3 > 0.0) {
            double loop = sim_time - last_departure3;

            loop_total += loop;
            departures3++;

            if (departures3 == 1 || loop < loop_min) loop_min = loop;
            if (loop > loop_max) loop_max = loop;
        }
        last_departure3 = sim_time;
    }

    bus_leave();
}
