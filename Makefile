NAME      := ircserv

CXX       := c++
CXXFLAGS  := -Wall -Wextra -Werror -std=c++98 -pedantic-errors
DEPFLAGS  := -MMD -MP
INCLUDES  := -I include -I include/commands

SRCDIR    := src
OBJDIR    := obj
SRCS      := $(sort $(wildcard $(SRCDIR)/*.cpp) $(wildcard $(SRCDIR)/*/*.cpp))

OBJS      := $(SRCS:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)
DEPS      := $(OBJS:.o=.d)

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf $(OBJDIR)

fclean: clean
	rm -f $(NAME)

re: fclean all

-include $(DEPS)

.PHONY: all clean fclean re
