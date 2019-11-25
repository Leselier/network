#include "sender.h"

#define U_TIMEOUT 90000

void init_sender(Sender * sender, int id)
{
    //TODO: You should fill in this function as necessary
    sender->send_id = id;
    sender->input_cmdlist_head = NULL;
    sender->input_framelist_head = NULL;
    pthread_cond_init(&sender->buffer_cv, NULL);
    pthread_mutex_init(&sender->buffer_mutex, NULL);
    sender->lar = AS-1;
    sender->lfs = AS-1;

    int i;
    for(i = 0; i < AS; i++)
        sender->sendQ[i].hasack = 0;
}

struct timeval * sender_get_next_expiring_timeval(Sender * sender)
{
    //TODO: You should fill in this function so that it returns the next timeout that should occur

    if(sender->lar == sender->lfs)
        return NULL;

    int i = ((sender->lar)+1)%AS;
    int has_min_time = 0;
    struct timeval *tmvl;
    struct timeval ctv;
    tmvl = (struct timeval *)malloc(sizeof(struct timeval));
    gettimeofday(tmvl,NULL);
    gettimeofday(&ctv,NULL);
    tmvl->tv_sec += 5;

    while((i <= sender->lfs && sender->lfs - i < SWS) || (i > sender->lfs && i - sender->lfs > AS - SWS))
    {
        if(!sender->sendQ[i].hasack && (sender->sendQ[i].tmout.tv_sec > ctv.tv_sec || (sender->sendQ[i].tmout.tv_sec == ctv.tv_sec && sender->sendQ[i].tmout.tv_usec > ctv.tv_usec))&& (sender->sendQ[i].tmout.tv_sec < tmvl->tv_sec || (sender->sendQ[i].tmout.tv_sec == tmvl->tv_sec && sender->sendQ[i].tmout.tv_usec < tmvl->tv_usec)))
        {
            tmvl->tv_sec = sender->sendQ[i].tmout.tv_sec;
            tmvl->tv_usec = sender->sendQ[i].tmout.tv_usec;
            has_min_time = 1;
        }
        i = (i + 1) % AS;
    }

    if(has_min_time)
        return tmvl;

    else
        return NULL;
}


void handle_incoming_acks(Sender * sender,
                          LLnode ** outgoing_frames_head_ptr)
{
    //TODO: Suggested steps for handling incoming ACKs
    //    1) Dequeue the ACK from the sender->input_framelist_head
    //    2) Convert the char * buffer to a Frame data type
    //    3) Check whether the frame is corrupted
    //    4) Check whether the frame is for this sender
    //    5) Do sliding window protocol for sender/receiver pair
    int incoming_ack_length = ll_get_length(sender->input_framelist_head);
    while(incoming_ack_length > 0)
    {
        LLnode * ll_inack_node = ll_pop_node(&sender->input_framelist_head);
        incoming_ack_length = ll_get_length(sender->input_framelist_head);

        char * raw_ack_buf = (char*)ll_inack_node->value;
        Frame * inackframe = convert_char_to_frame(raw_ack_buf);

        free(raw_ack_buf);

        if(!verify_add(inackframe,inackframe->add))
            goto discard;

        if(inackframe->to_id != sender->send_id)
            goto discard;
        if(inackframe->type != 0)
            goto discard;

        if((inackframe->seq_num <= sender->lfs && sender->lfs - inackframe->seq_num < SWS) || (inackframe->seq_num > sender->lfs && inackframe->seq_num - sender->lfs > AS - SWS))
        {
            if(sender->sendQ[inackframe->seq_num].hasack == 1) goto discard;

            sender->sendQ[inackframe->seq_num].hasack = 1;
            free(sender->sendQ[inackframe->seq_num].fs);
            sender->sendQ[inackframe->seq_num].fs = NULL;
        }

discard:
        free(inackframe);

    while(sender->sendQ[((sender->lar)+1)%AS].hasack && sender->lar != sender->lfs)
    {
        sender->lar = ((sender->lar)+1)%AS;
        sender->sendQ[sender->lar].hasack = 0;
    }

    free(ll_inack_node);

    }
}


void handle_input_cmds(Sender * sender,
                       LLnode ** outgoing_frames_head_ptr)
{
    //TODO: Suggested steps for handling input cmd
    //    1) Dequeue the Cmd from sender->input_cmdlist_head
    //    2) Convert to Frame
    //    3) Set up the frame according to the sliding window protocol
    //    4) Compute CRC and add CRC to Frame

    int input_cmd_length = ll_get_length(sender->input_cmdlist_head);
        
    //Recheck the command queue length to see if stdin_thread dumped a command on us
    input_cmd_length = ll_get_length(sender->input_cmdlist_head);
    while (input_cmd_length > 0 && ((sender->lar <= sender->lfs && sender->lfs - sender->lar < SWS) || (sender->lar > sender->lfs && sender->lar - sender->lfs > AS - SWS +1)))
    {
        //Pop a node off and update the input_cmd_length
        LLnode * ll_input_cmd_node = ll_pop_node(&sender->input_cmdlist_head);
        input_cmd_length = ll_get_length(sender->input_cmdlist_head);

        //Cast to Cmd type and free up the memory for the node
        Cmd * outgoing_cmd = (Cmd *) ll_input_cmd_node->value;
        free(ll_input_cmd_node);

        //DUMMY CODE: Add the raw char buf to the outgoing_frames list
        //NOTE: You should not blindly send this message out!
        //      Ask yourself: Is this message actually going to the right receiver (recall that default behavior of send is to broadcast to all receivers)?
        //                    Does the receiver have enough space in in it's input queue to handle this message?
        //                    Were the previous messages sent to this receiver ACTUALLY delivered to the receiver?
        int msg_length = strlen(outgoing_cmd->message);
        if (msg_length > FRAME_PAYLOAD_SIZE - 2)
        {
            //Do something about messages that exceed the frame size
            //printf("<SEND_%d>: sending messages of length greater than %d is not implemented\n", sender->send_id, MAX_FRAME_SIZE);

            char *n_cmd_msg  = (char *) malloc(msg_length * sizeof(char));
            strcpy(n_cmd_msg,outgoing_cmd->message + (FRAME_PAYLOAD_SIZE - 1));
            outgoing_cmd->message[FRAME_PAYLOAD_SIZE-1] = '\0';

            Cmd * n_cmd = (Cmd *) malloc(sizeof(Cmd));
            n_cmd->src_id = outgoing_cmd->src_id;
            n_cmd->dst_id = outgoing_cmd->dst_id;
            n_cmd->message = n_cmd_msg;

            ll_append_node(&sender->input_cmdlist_head,(void*)n_cmd);
        }


        if(outgoing_cmd->src_id != sender->send_id || outgoing_cmd->dst_id > glb_receivers_array_length || outgoing_cmd->dst_id < 0)
        {
            free(outgoing_cmd->message);
            free(outgoing_cmd);
        }
        else
        {
            //This is probably ONLY one step you want
            Frame * outgoing_frame = (Frame *) malloc (sizeof(Frame));
            
            memset(outgoing_frame->data,0,sizeof(FRAME_PAYLOAD_SIZE));
            strcpy(outgoing_frame->data, outgoing_cmd->message);
            outgoing_frame->to_id = outgoing_cmd->dst_id;
            outgoing_frame->from_id = outgoing_cmd->src_id;

            sender->lfs = ((sender->lfs)+1)%AS;
            outgoing_frame->seq_num = sender->lfs;
            outgoing_frame->type = 1;
            
            outgoing_frame->add = add32(outgoing_frame);
    
            int i = outgoing_frame->seq_num;
            sender->sendQ[i].hasack = 0;
            sender->sendQ[i].fs = (Frame *)malloc (sizeof(Frame));
            memcpy(sender->sendQ[i].fs,outgoing_frame,sizeof(Frame));
            
            struct timeval tv;
            gettimeofday(&tv,NULL);
            tv.tv_usec += U_TIMEOUT;
            if(tv.tv_usec >= 1000000)
            {
                tv.tv_sec += 1;
                tv.tv_usec -= 1000000;
            }
            sender->sendQ[i].tmout.tv_sec = tv.tv_sec;
            sender->sendQ[i].tmout.tv_usec = tv.tv_usec;
            sender->sendQ[i].hasack = 0;

            //printf("sendQ[%d] to%d, from %d, seq %d, msg:%s, tv_sec %d, tv_usc%d",i,sender->sendQ[i].fs->to_id,sender->sendQ[i].fs->from_id,sender->sendQ[i].fs->seq_num,sender->sendQ[i].fs->data,sender->sendQ[i].tmout.tv_sec,sender->sendQ[i].tmout.tv_usec);

            
            //printf("sending message:src %d dst %d, seq %d ,lar %d, lfs %d\n",outgoing_frame->to_id,outgoing_frame->from_id,outgoing_frame->seq_num,sender->lar,sender->lfs);

            //At this point, we don't need the outgoing_cmd
            free(outgoing_cmd->message);
            free(outgoing_cmd);

            //Convert the message to the outgoing_charbuf
            char * outgoing_charbuf = convert_frame_to_char(outgoing_frame);
            ll_append_node(outgoing_frames_head_ptr,
                           outgoing_charbuf);
            free(outgoing_frame);
        }
    }   
}


void handle_timedout_frames(Sender * sender,
                            LLnode ** outgoing_frames_head_ptr)
{
    //TODO: Suggested steps for handling timed out datagrams
    //    1) Iterate through the sliding window protocol information you maintain for each receiver
    //    2) Locate frames that are timed out and add them to the outgoing frames
    //    3) Update the next timeout field on the outgoing frames
    
    struct timeval ctv;
    gettimeofday(&ctv,NULL);
    
    int i = ((sender->lar)+1)%AS;
    
    while((i <= sender->lfs && sender->lfs - i < SWS) || (i > sender->lfs && i - sender->lfs > AS - SWS))
    {
        if(!sender->sendQ[i].hasack && (sender->sendQ[i].tmout.tv_sec <= ctv.tv_sec || (sender->sendQ[i].tmout.tv_sec == ctv.tv_sec && sender->sendQ[i].tmout.tv_usec <= ctv.tv_sec)))
        {
            Frame * outgoing_frame = (Frame *) malloc (sizeof(Frame));
            memcpy(outgoing_frame,sender->sendQ[i].fs,sizeof(Frame));
            //printf("resending message:src %d dst %d, seq %d,lar %d, lfs %0d\n",outgoing_frame->to_id,outgoing_frame->from_id,outgoing_frame->seq_num,sender->lar,sender->lfs);
            char * outgoing_charbuf = convert_frame_to_char(outgoing_frame);
            ll_append_node(outgoing_frames_head_ptr,outgoing_charbuf);
            free(outgoing_frame);

            sender->sendQ[i].tmout.tv_sec = ctv.tv_sec;
            sender->sendQ[i].tmout.tv_usec = ctv.tv_usec + U_TIMEOUT;
            
            if(sender->sendQ[i].tmout.tv_usec > 1000000)
            {
                sender->sendQ[i].tmout.tv_sec += 1;
                sender->sendQ[i].tmout.tv_usec -= 1000000;
            }
        }

        i = (i + 1) % AS;
    }        
}


void * run_sender(void * input_sender)
{    
    struct timespec   time_spec;
    struct timeval    curr_timeval;
    const int WAIT_SEC_TIME = 0;
    const long WAIT_USEC_TIME = 100000;
    Sender * sender = (Sender *) input_sender;    
    LLnode * outgoing_frames_head;
    struct timeval * expiring_timeval;
    long sleep_usec_time, sleep_sec_time;
    
    //This incomplete sender thread, at a high level, loops as follows:
    //1. Determine the next time the thread should wake up
    //2. Grab the mutex protecting the input_cmd/inframe queues
    //3. Dequeues messages from the input queue and adds them to the outgoing_frames list
    //4. Releases the lock
    //5. Sends out the messages


    while(1)
    {    
        outgoing_frames_head = NULL;

        //Get the current time
        gettimeofday(&curr_timeval, 
                     NULL);

        //time_spec is a data structure used to specify when the thread should wake up
        //The time is specified as an ABSOLUTE (meaning, conceptually, you specify 9/23/2010 @ 1pm, wakeup)
        time_spec.tv_sec  = curr_timeval.tv_sec;
        time_spec.tv_nsec = curr_timeval.tv_usec * 1000;

        //Check for the next event we should handle
        expiring_timeval = sender_get_next_expiring_timeval(sender);

        //Perform full on timeout
        if (expiring_timeval == NULL)
        {
            time_spec.tv_sec += WAIT_SEC_TIME;
            time_spec.tv_nsec += WAIT_USEC_TIME * 1000;
        }
        else
        {
            //Take the difference between the next event and the current time
            sleep_usec_time = timeval_usecdiff(&curr_timeval,
                                               expiring_timeval);

            //Sleep if the difference is positive
            if (sleep_usec_time > 0)
            {
                sleep_sec_time = sleep_usec_time/1000000;
                sleep_usec_time = sleep_usec_time % 1000000;   
                time_spec.tv_sec += sleep_sec_time;
                time_spec.tv_nsec += sleep_usec_time*1000;
            }   
        }

        //Check to make sure we didn't "overflow" the nanosecond field
        if (time_spec.tv_nsec >= 1000000000)
        {
            time_spec.tv_sec++;
            time_spec.tv_nsec -= 1000000000;
        }

        
        //*****************************************************************************************
        //NOTE: Anything that involves dequeing from the input frames or input commands should go 
        //      between the mutex lock and unlock, because other threads CAN/WILL access these structures
        //*****************************************************************************************
        pthread_mutex_lock(&sender->buffer_mutex);

        //Check whether anything has arrived
        int input_cmd_length = ll_get_length(sender->input_cmdlist_head);
        int inframe_queue_length = ll_get_length(sender->input_framelist_head);
        
        //Nothing (cmd nor incoming frame) has arrived, so do a timed wait on the sender's condition variable (releases lock)
        //A signal on the condition variable will wakeup the thread and reaquire the lock
        if (input_cmd_length == 0 &&
            inframe_queue_length == 0)
        {
            
            pthread_cond_timedwait(&sender->buffer_cv, 
                                   &sender->buffer_mutex,
                                   &time_spec);
        }
        //Implement this
        handle_incoming_acks(sender,
                             &outgoing_frames_head);

        //Implement this
        handle_input_cmds(sender,
                          &outgoing_frames_head);

        pthread_mutex_unlock(&sender->buffer_mutex);


        //Implement this
        handle_timedout_frames(sender,
                               &outgoing_frames_head);

        //CHANGE THIS AT YOUR OWN RISK!
        //Send out all the frames
        int ll_outgoing_frame_length = ll_get_length(outgoing_frames_head);
        
        while(ll_outgoing_frame_length > 0)
        {
            LLnode * ll_outframe_node = ll_pop_node(&outgoing_frames_head);
            char * char_buf = (char *)  ll_outframe_node->value;

            //Don't worry about freeing the char_buf, the following function does that
            send_msg_to_receivers(char_buf);

            //Free up the ll_outframe_node
            free(ll_outframe_node);

            ll_outgoing_frame_length = ll_get_length(outgoing_frames_head);
        }
    }
    pthread_exit(NULL);
    return 0;
}
