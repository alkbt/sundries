OBJS = critical_section.o \
       deferred_delete_ref_counter.o \
       double_word_cas_ref_counter.o \
       lf-problems.o \
       producer_consumer.o \
       thread_safe_singleton.o

all		:	$(OBJS)
	@echo "Building lf-problems"
	@clang++ -pthreads -fsanitize=thread -o lf-problems $(OBJS)
	@echo "Running lf-problems"
	@./lf-problems

clean	:
	rm *.o
	rm ./lf-problems

%.o : %.cpp
	clang++ -c -g -O3 --std=c++11 $< -o $@