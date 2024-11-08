void
scheduler(void)
{
  struct proc *p;
  struct proc *shortestExecutionTimeP;
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run. -needs outer loop to processes, not sure why
    acquire(&ptable.lock);
	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
		if(p->state != RUNNABLE)
          continue;
		shortestExecutionTimeP = p;//ensure that shortestExecutionTimeP is initialized
	//loop through process table for the process with the shortest Execution time
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
       if(p->state == RUNNABLE && p->executionTime < shortestExecutionTimeP->executionTime)
	   	shortestExecutionTimeP = p;
	}
      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
	  p= shortestExecutionTimeP;
      proc = p;

      switchuvm(p);
      p->state = RUNNING;
	 //reset this to current time for use when calculating execution
	 // time of process
	  burstStartTime = ticks;
	  //set how long process can run for
	  proc->maxExecutionTime = ((ticks-proc->startTime)/numProcesses);
	  //cprintf("SCHEDULER DEBUG(%d): time: %d, maxExecutionTime is %d\n",proc->pid,ticks, proc->maxExecutionTime);
      swtch(&cpu->scheduler, proc->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      proc = 0;
  }
    release(&ptable.lock);

  }
}