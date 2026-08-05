NAME      := ircserv

CXX       := c++
CXXFLAGS  := -Wall -Wextra -Werror -std=c++98 -pedantic-errors
DEPFLAGS  := -MMD -MP
INCLUDES  := -I include -I include/commands

SRCDIR    := src
OBJDIR    := obj

FILES     := main \
             Server \
             Socket \
             Client \
             Message \
             Reply \
             StringUtil \
             IrcException \
             Log \
             ACommand \
             CommandDispatcher \
             Channel/Channel \
             Channel/Channel_members \
             Channel/Channel_operator \
             Channel/Channel_invite \
             Channel/Channel_topic \
             Channel/Channel_mode \
             Channel/Channel_broadcast \
             commands/Pass \
             commands/Nick \
             commands/User \
             commands/Cap \
             commands/Quit \
             commands/Ping \
             commands/Join \
             commands/Part \
             commands/Privmsg \
             commands/Notice \
             commands/Topic \
             commands/Kick \
             commands/Invite \
             commands/Mode \
             commands/Who

SRCS      := $(FILES:%=$(SRCDIR)/%.cpp)
OBJS      := $(FILES:%=$(OBJDIR)/%.o)
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
