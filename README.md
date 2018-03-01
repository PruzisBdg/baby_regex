# baby_regex
This is a regex for small embedded systems. It's not fully compliant but it 
will do 99% of what typical users need. Features are:

1. It mallocs for threads etc up-front, before compiling and running the regex.
   So efficient witht eh heap and no nasty surprises.
   
2. Is non-backtracking multi-threaded using a non deterministic finite automaton, 
   like Plan9. So doesn't blow up large inputs and/or expressions.
   
3. It merges duplicate thread-states. This avoids blowing up with explosive
   quantifiers, where the NFA will reach the same state by multiple routes.