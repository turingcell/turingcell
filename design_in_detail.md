# Detailed Design of TuringCell

Status: Not Finished Yet

## Key-Value Space Layout

```
kv
    turingcell_computer_0
        cpu_state
        ram_state
        io_devices
            timer_state
            uart_state
            disk_state
            interrupt_controller_state
    ...
    turingcell_computer_n
```

## Interact with Outside

sequence_number

ts

cpu clock cycle; instruction counter

```
mdf(proposer_ts,mono_global_sequence_number)
```

## Data Structure

```
cpu ram io_device_registers
io_device{ 
    private_ram,
    private_durable_storage
}

pre_cpu_exec_phase
    io_device_pre_cpu_exec_phase_handler
        timer
        uart
        disk
        interrupt_controller
        ...
cpu_exec_phase
    cpu_exec_batch(exact_cpuclk_amount_to_run)
        armv4_cpu
    io_device_registers_read/write_handler()
        timer
        uart
        disk
        interrupt_controller
        ...
    io_device_cpuclk_timer_routine
        timer
        ...
post_cpu_exec_phase
    io_device_post_cpu_exec_phase_handler
        timer
        uart
        disl
        interrupt_controller
        ...
```
