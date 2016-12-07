/*
 Hanoh Haim
 Cisco Systems, Inc.
*/

/*
Copyright (c) 2015-2015 Cisco Systems, Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "h_timer.h"


void CHTimerObj::Dump(FILE *fd){
    fprintf(fd,"m_tick_left        :%lu \n", (ulong)m_ticks_left);
}


RC_HTW_t CHTimerOneWheel::Create(uint32_t wheel_size){

    CHTimerWheelLink *bucket;

    if ( !utl_islog2(wheel_size) ){
        return (RC_HTW_ERR_NO_LOG2);
    }
    m_wheel_mask = utl_mask_log2(wheel_size);
    m_wheel_size  = wheel_size;

    m_buckets = (CHTimerWheelLink *)malloc(wheel_size * sizeof(CHTimerWheelLink));
    //printf(" bucket %x \n",);
    if (m_buckets == 0) {
        return (RC_HTW_ERR_NO_RESOURCES);
    }
    m_ticks=0;
    m_bucket_index = 0;
    m_tick_done=false;

    bucket = &m_buckets[0];
    m_active_bucket=bucket;
    int i;
    for (i = 0; i < wheel_size; i++) {
        bucket->set_self();
        bucket++;
    }
    return (RC_HTW_OK);
}

RC_HTW_t CHTimerOneWheel::Delete(){
    if (m_buckets) {
        free(m_buckets);
        m_buckets=0;
    }
    m_ticks=0;
    m_bucket_index = 0;
    return (RC_HTW_OK );
}


RC_HTW_t CHTimerOneWheel::timer_stop(CHTimerObj *tmr){
    if ( tmr->is_running() ) {
        tmr->detach();
    }
    return (RC_HTW_OK);
}


void  CHTimerOneWheel::dump_link_list(uint32_t bucket_index,
                                      void *userdata,
                                      htw_on_tick_cb_t cb,
                                      FILE *fd){


    CHTimerWheelLink  *bucket, *next;
    CHTimerObj *tmr;
    bucket = &m_buckets[bucket_index];

    tmr = (CHTimerObj *)bucket->m_next;
    bool found=false;
    if ((CHTimerWheelLink *)tmr != bucket) {
        fprintf(fd,"[%lu,\n",(ulong)bucket_index);
        found=true;
    }

    while( (CHTimerWheelLink *)tmr != bucket) {

        next = (CHTimerWheelLink *)tmr->m_next;

        tmr->Dump(fd);
        cb(userdata,tmr);

        tmr = (CHTimerObj *)next;
    }
    if (found){
        fprintf(fd,"]\n");
    }
}


void CHTimerWheel::on_tick(void *userdata,htw_on_tick_cb_t cb){

    int i;


    for (i=0;i<m_num_wheels; i++) {
        CHTimerOneWheel * lp=&m_timer_w[i];
        CHTimerObj * event;

        while (  true ) {
            event = lp->pop_event();
            if (!event) {
                break;
            }
            m_total_events--;
            if (event->m_ticks_left==0) {
                cb(userdata,event);
            }else{
                timer_start(event,event->m_ticks_left); 
            }
        }
        if (!lp->check_timer_tick_cycle()){
            break;
        }
    }

    /* tick all timers in one shoot */
    for (i=0;i<m_num_wheels; i++) {
        CHTimerOneWheel * lp=&m_timer_w[i];
        if (!lp->timer_tick()){
            break;
        }
    }
    m_ticks++;
}



RC_HTW_t CHTimerWheel::timer_stop (CHTimerObj *tmr){
    if ( tmr->is_running() ) {
        assert(tmr->m_wheel<m_num_wheels);
        m_timer_w[tmr->m_wheel].timer_stop(tmr);
        m_total_events--;
    }
    return (RC_HTW_OK);
}



RC_HTW_t CHTimerWheel::timer_start_rest(CHTimerObj  *tmr, 
                                        htw_ticks_t  ticks){
    int i;
    htw_ticks_t nticks  = ticks;
    htw_ticks_t total_shift = 0;
    htw_ticks_t residue_diff = m_timer_w[0].get_bucket_index();

    for (i=1; i<m_num_wheels; i++) {
        nticks = nticks>>m_wheel_shift;
        total_shift += m_wheel_shift;

        if (likely(nticks<m_wheel_size)) {
            tmr->m_wheel=i;
            tmr->m_ticks_left = ticks - ((nticks<<total_shift)-residue_diff) ;
            m_timer_w[i].timer_start(tmr,nticks);
            return(RC_HTW_OK);
        }
        residue_diff += (m_timer_w[i].get_bucket_index()<<total_shift);

    }
    tmr->m_wheel=i-1;
    residue_diff -= (m_timer_w[i-1].get_bucket_index()<<total_shift);
    tmr->m_ticks_left = ticks - ((m_wheel_mask<<total_shift)-residue_diff);
    m_timer_w[i-1].timer_start(tmr,m_wheel_mask);
    return (RC_HTW_OK);
}


void CHTimerWheel::reset(){
    m_wheel_shift=0;
    m_num_wheels=0;
    m_ticks=0;
    m_total_events=0;
    m_wheel_size=0;
    m_wheel_mask=0;
}


RC_HTW_t CHTimerWheel::Create(uint32_t wheel_size,
                              uint32_t num_wheels){
    RC_HTW_t res;
    if (num_wheels>MAX_H_TIMER_WHEELS) {
        return (RC_HTW_ERR_MAX_WHEELS);
    }
    m_num_wheels=num_wheels;
    int i;
    for (i=0; i<num_wheels; i++) {
        res = m_timer_w[i].Create(wheel_size);
        if ( res !=RC_HTW_OK ){
            return (res);
        }
    }
    m_ticks =0;
    m_wheel_shift = utl_log2_shift(wheel_size);
    if ( m_wheel_shift * num_wheels > (sizeof(htw_ticks_t)*8)) {
        return(RC_HTW_ERR_NOT_ENOUGH_BITS);
    }
    m_wheel_mask  = utl_mask_log2(wheel_size);
    m_wheel_size  = wheel_size;
    return(RC_HTW_OK);
}

RC_HTW_t CHTimerWheel::Delete(){
    int i;
    for (i=0; i<m_num_wheels; i++) {
        m_timer_w[i].Delete();
    }
    return(RC_HTW_OK);
}




