


void* async_send_negotiator()
{
    uint32_t t;

    goto rx_active_state;
    
idle_state:

    if (!fifo.empty()) goto start_sending_state;
    AWAIT(rx_event, fifo_event);
    if (rx_event.get_and_clear()) {
        goto rx_active_state;
    } else {
        goto start_sending_state;
    }

start_sending_state:

    fifo_event.clear();
    if (fifo.empty()) goto idle_state;
    timer_event.clear_and_set_timeout(uart_bits_time(17));
    start_tx_pin_monitor(); // Connect external interrupte with AFIO_EXTICRn and EXTI_FTSR+EXTI_RTSR (also clear EXTI_PR)
    AWAIT(timer_event);
    if (tx_pin_activity_detected()) { // Check current line state and EXTI_PR
        goto rx_active_state;
    } else {
        goto send_startup_state;
    }

send_startup_state:

    switch_rx_to_6_bytes_direct_buffer();
    send_startup_sequence();
    stop_tx_pin_monitor();
    timer_event.clear_and_set_timeout(uart_bits_time(11 * 6 + 17));
    AWAIT(rx_direct_buffer_event, timer_event);
    
    if (timer_event.get_and_clear()) {
        t = line_collision_rand_time();
        goto line_collision;
    }
    if (rx_direct_buffer_event.get_and_clear()) {
        t = verify_startup_sequence();
        if (t > 0) goto line_collision;
        goto tx_start;
    }

tx_start:

    total_send = 0;
    while (!fifo.empty() && total_send < MAX_MULTIPLE_PACKETS_BYTES) {
        tx_sent_event.clear();
        total_send += send_packet_from_fifo();
        AWAIT(tx_sent_event);
    }
    send_stop_symbol();
    goto rx_active_state;

line_collision:
    timer_event.clear_and_set_timeout(t);
    AWAIT(timer_event);
    goto send_startup_state;

rx_active_state:

    timer_event.clear_and_set_timeout(get_rx_wait_time(is_ended_with_stop()));
    AWAIT(rx_event, timer_event);
    if (rx_event.get_and_clear()) goto rx_active_state;
    if (timer_event.get_and_clear()) goto idle_state;
}
