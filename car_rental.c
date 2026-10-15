#include "simlib.h"

/*
 * SIMLIB model of the rental-car shuttle.
 * All simulation times are in minutes (80 hours = 4,800 minutes).
 * A1/A2/A3 are passenger-arrival events; BUS_ARRIVE, STOP_END, and BUS_LEAVE
 * move the shuttle through its route. END_RUN stops the model at 80 hours.
 */
#define A1 1
#define A2 2
#define A3 3
#define BUS_ARRIVE 4
#define BUS_LEAVE 5
#define STOP_END 6
#define END_RUN 7
#define Q1 1
#define Q2 2
#define Q3 3
#define HORIZON 4800.0
#define CAPACITY 20
#define STREAM_DEST 4
#define STREAM_UNLOAD 5
#define STREAM_LOAD 6

/* Information kept for each passenger after boarding the bus. */
typedef struct { int destination, origin; double arrival; } Passenger;
static Passenger onboard[CAPACITY];
static int n_onboard, location=3, queue_id[3]={Q1,Q2,Q3};
static int max_queue[3], max_onboard, departures3;
static double arrivals[3], q_area[3], q_clock[3];
static double delay_total[3], delay_max[3], delay_count[3];
static double bus_area, bus_clock, person_total, person_max, people_done;
static double stop_total[3], stop_min[3], stop_max[3], stop_count[3];
static double loop_total, loop_min, loop_max, last_departure3;
static double trip_total[3], trip_max[3], trip_count[3];
static double stop_start, dwell, run_unload, onboard_arrival[CAPACITY];
static int unloading, next_location;
static FILE *out;

static void arrival(int loc);
static void bus_arrival(void);
static void stop_end(void);
static void bus_leave(void);

int main(void)
{
    int event;
    out=fopen("car_rental.out","w"); if(!out) return 1;
    /* Initialize SIMLIB's event/list machinery and fix all random streams. */
    init_simlib(); maxatr=4; maxlist=7;
    for(int s=1;s<=6;s++) lcgrandst(123456789L+97L*s,s); /* fixed streams */
    /* Schedule first passenger arrivals. Rates are people/hour, so convert
       their mean interarrival times to minutes before calling expon(). */
    event_schedule(expon(60.0/14.0,1),A1);
    event_schedule(expon(60.0/10.0,2),A2);
    event_schedule(expon(60.0/24.0,3),A3);
    /* The bus starts at the rental location and leaves immediately. */
    event_schedule(0.0,BUS_LEAVE); event_schedule(HORIZON,END_RUN);
    while(1) {
        timing(); event=next_event_type;
        if(event==END_RUN) break;
        if(event==A1) arrival(1); else if(event==A2) arrival(2);
        else if(event==A3) arrival(3); else if(event==BUS_ARRIVE) bus_arrival();
        else if(event==BUS_LEAVE) bus_leave(); else if(event==STOP_END) stop_end();
    }
    /* Extend time-weighted queue and bus statistics to the 80-hour cutoff. */
    bus_area+=n_onboard*(HORIZON-bus_clock);
    for(int i=0;i<3;i++) q_area[i]+=list_size[queue_id[i]]*(HORIZON-q_clock[i]);
    fprintf(out,"CAR-RENTAL SHUTTLE - SIMLIB / C - 80 hours\nSeed: 123456789\n");
    fprintf(out,"\n(a) QUEUE LENGTH\nLocation  Arrivals  Average number  Maximum number\n");
    for(int i=0;i<3;i++) fprintf(out,"   %d       %5.0f       %8.3f          %d\n",i+1,arrivals[i],q_area[i]/HORIZON,max_queue[i]);
    fprintf(out,"\n(b) QUEUE DELAY (minutes)\nLocation  Average  Maximum\n");
    for(int i=0;i<3;i++) fprintf(out,"   %d       %7.3f  %7.3f\n",i+1,delay_count[i]?delay_total[i]/delay_count[i]:0.0,delay_max[i]);
    fprintf(out,"\n(c) BUS LOAD\nAverage: %.3f people | Maximum: %d people\n",bus_area/HORIZON,max_onboard);
    fprintf(out,"\n(d) STOP TIME BY LOCATION (minutes)\nLocation  Average  Minimum  Maximum\n");
    for(int i=0;i<3;i++) fprintf(out,"   %d       %7.3f  %7.3f  %7.3f\n",i+1,stop_count[i]?stop_total[i]/stop_count[i]:0.0,stop_count[i]?stop_min[i]:0.0,stop_max[i]);
    fprintf(out,"\n(e) LOOP: departure from rental to next departure (minutes)\nAverage %.3f | Minimum %.3f | Maximum %.3f\n",departures3?loop_total/departures3:0.0,departures3?loop_min:0.0,loop_max);
    fprintf(out,"\n(f) PERSON TIME IN SYSTEM BY ARRIVAL LOCATION (minutes)\nLocation  Average  Maximum  Completed\n");
    for(int i=0;i<3;i++) fprintf(out,"   %d       %7.3f  %7.3f    %6.0f\n",i+1,trip_count[i]?trip_total[i]/trip_count[i]:0.0,trip_max[i],trip_count[i]);
    fprintf(out,"Overall: average %.3f | maximum %.3f | completed trips %.0f\n",people_done?person_total/people_done:0.0,person_max,people_done);
    fclose(out); return 0;
}

static void arrival(int loc)
{
    int q=queue_id[loc-1]; double rate[3]={14.0,10.0,24.0};
    /* Schedule the next arrival at this location (Poisson process). */
    event_schedule(sim_time+expon(60.0/rate[loc-1],loc),loc);
    /* Update queue area, then append this person's arrival time at FIFO tail. */
    q_area[loc-1]+=list_size[q]*(sim_time-q_clock[loc-1]); q_clock[loc-1]=sim_time;
    transfer[1]=sim_time; list_file(LAST,q); arrivals[loc-1]++;
    q_area[loc-1]+=list_size[q]*(sim_time-q_clock[loc-1]); q_clock[loc-1]=sim_time;
    if(list_size[q]>max_queue[loc-1]) max_queue[loc-1]=list_size[q];
}

static void bus_leave(void)
{
    /* Route: rental (3) -> terminal 1 -> terminal 2 -> rental (3).
       Distances are 4.5, 1, and 4.5 miles; speed is 30 miles/hour. */
    double miles=(location==3)?4.5:((location==1)?1.0:4.5);
    next_location=(location==3)?1:((location==1)?2:3);
    event_schedule(sim_time+miles*60.0/30.0,BUS_ARRIVE);
}

static void bus_arrival(void)
{
    int kept=0, loc=next_location;
    bus_area+=n_onboard*(sim_time-bus_clock); bus_clock=sim_time;
    location=loc; stop_start=sim_time; run_unload=0.0; unloading=0;
    /* Let off passengers whose destination is this stop. The others stay on. */
    for(int i=0;i<n_onboard;i++) {
        if(onboard[i].destination==loc) {
            double trip=sim_time-onboard[i].arrival;
            person_total+=trip; people_done++;
            trip_total[onboard[i].origin-1]+=trip; trip_count[onboard[i].origin-1]++;
            if(trip>person_max) person_max=trip;
            if(trip>trip_max[onboard[i].origin-1]) trip_max[onboard[i].origin-1]=trip;
            unloading++;
        } else onboard[kept++]=onboard[i];
    }
    n_onboard=kept;
    /* Unloading is completed before the STOP_END event begins boarding. */
    run_unload=uniform(16.0/60.0,24.0/60.0,STREAM_UNLOAD)*unloading;
    dwell=run_unload;
    event_schedule(sim_time+run_unload,STOP_END);
}

static void stop_end(void)
{
    int q=queue_id[location-1];
    /* Board from the front of the location's FIFO queue until the bus is full. */
    while(n_onboard<CAPACITY && list_size[q]>0) {
        q_area[location-1]+=list_size[q]*(sim_time-q_clock[location-1]); q_clock[location-1]=sim_time;
        list_remove(FIRST,q);
        q_area[location-1]+=list_size[q]*(sim_time-q_clock[location-1]); q_clock[location-1]=sim_time;
        /* Queue delay: time from joining the queue until leaving it to board. */
        double delay=sim_time-transfer[1];
        delay_total[location-1]+=delay; delay_count[location-1]++;
        if(delay>delay_max[location-1]) delay_max[location-1]=delay;
        onboard[n_onboard].arrival=transfer[1];
        onboard[n_onboard].origin=location;
        /* Terminal passengers go to rental; rental passengers choose a
           terminal using the probabilities given in the problem statement. */
        onboard[n_onboard].destination=(location==3)?((uniform(0.0,1.0,STREAM_DEST)<0.583)?1:2):3;
        dwell+=uniform(15.0/60.0,25.0/60.0,STREAM_LOAD);
        n_onboard++;
    }
    if(n_onboard>max_onboard) max_onboard=n_onboard;
    bus_area+=n_onboard*(sim_time-bus_clock); bus_clock=sim_time;
    /* The bus must spend at least five minutes at each location. */
    if(dwell<5.0) dwell=5.0;
    stop_total[location-1]+=dwell; stop_count[location-1]++;
    if(stop_min[location-1]==0.0 || dwell<stop_min[location-1]) stop_min[location-1]=dwell;
    if(dwell>stop_max[location-1]) stop_max[location-1]=dwell;
    /* A loop is measured from one departure at rental to the next. */
    if(location==3) {
        if(last_departure3>0.0) {
            double loop=sim_time+dwell-last_departure3;
            loop_total+=loop; departures3++;
            if(departures3==1 || loop<loop_min) loop_min=loop;
            if(loop>loop_max) loop_max=loop;
        }
        last_departure3=sim_time+dwell;
    }
    event_schedule(stop_start+dwell,BUS_LEAVE);
}
