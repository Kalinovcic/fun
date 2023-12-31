#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>

EnterApplicationNamespace


#if 0
static User* current_user;


struct User
{
    byte* user_memory;
    umm   user_memory_size;
    umm   next_allocation_offset;

    struct
    {
        byte* base;
        umm   size;
        int   prot;
    }   frozen_memory[1024];
    umm frozen_memory_count;
    int          maps_fd;
    sighandler_t previous_sigsegv_handler;

    u32    most_recently_executed_line;
    String most_recently_executed_file;
};

User* create_user()
{
    assert(current_user == NULL);  // can't work with users as a user

    umm   user_memory_size = 1024 * 1024 * 1024;  // 1 GB
    byte* user_memory = (byte*) mmap(0, user_memory_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(user_memory);

    User* user = (User*) user_memory;
    user->user_memory      = user_memory;
    user->user_memory_size = user_memory_size;

    user->next_allocation_offset = sizeof(User);
    return user;
}

void delete_user(User* user)
{
    assert(current_user == NULL);  // can't work with users as a user
    munmap(user->user_memory, user->user_memory_size);
}


byte* user_alloc(User* user, umm size, umm alignment)
{
    assert(!current_user || current_user == user);  // can't work with other users as a user

    while (user->next_allocation_offset % alignment)
        user->next_allocation_offset++;
    byte* result = user->user_memory + user->next_allocation_offset;
    user->next_allocation_offset += size;

    if (user->next_allocation_offset > user->user_memory_size)
    {
        exit_lockdown(user);
        fprintf(stderr, "user code is out of memory\n");
        exit(1);
    }

    memset(result, 0xCD, size);
    return result;
}

void user_free(User* user, void* base)
{
    assert(!current_user || current_user == user);  // can't work with other users as a user
    // leaked
}


static void freeze_memory(User* user, byte* from, byte* to, int prot)
{
    assert(from <= to);
    if (from == to) return;

    volatile byte stack_address;
    if (from <= &stack_address && to > &stack_address)
        return;  // don't freeze the stack
    // @Incomplete - don't freeze other user thread stacks either

    if (from <= user->user_memory && to > user->user_memory)
    {
        assert(to >= user->user_memory + user->user_memory_size);
        freeze_memory(user, from,                                       user->user_memory, prot);
        freeze_memory(user, user->user_memory + user->user_memory_size, to,                prot);
        return;
    }

    if (user->frozen_memory_count < ArrayCount(user->frozen_memory))
    {
        umm size = to - from;
        umm i = user->frozen_memory_count++;
        user->frozen_memory[i].base = from;
        user->frozen_memory[i].size = size;
        user->frozen_memory[i].prot = prot;
        mprotect(from, size, prot & ~PROT_WRITE);
    }
}


static void sigsegv_handler(int signal)
{
    if (signal == SIGSEGV)
    {
        User* user = current_user;
        assert(user);
        exit_lockdown(user);

        auto error = [](String data)
        {
            int fd = 2;  // stderr
            while (data)
            {
                int amount = ::write(fd, data.data, data.length);
                if (amount <= 0 || amount > data.length) break;
                consume(&data, amount);
            }
        };

        error("\n\n\n"
              "ERROR PREVENTION:\n"
              "User code generated SIGSEGV.\n"_s);
        if (user->most_recently_executed_file)
        {
            umm digits_length = digits_base10_u64(user->most_recently_executed_line);
            u8  digits[32] = {};
            write_base10_u64(digits, digits_length, user->most_recently_executed_line);

            error("Last known location: "_s);
            error(user->most_recently_executed_file);
            error(":"_s);
            error({ digits_length, digits });
            error("\n"_s);
        }
        error("Aborting...\n"_s);
        raise(SIGKILL);
    }
}


void enter_lockdown(User* user)
{
    assert(current_user == NULL);  // can't work with users as a user
    current_user = user;

    // @Incomplete: stop the world

    user->previous_sigsegv_handler = ::signal(SIGSEGV, sigsegv_handler);

    // Freeze non-user memory
    int buffer_size = 0;
    u8  buffer[4096];
    int fd = ::open("/proc/self/maps", O_RDONLY);
    user->maps_fd = fd;
    if (fd >= 0)
    {
    read_more:
        if (buffer_size == sizeof(buffer)) goto done;  // can't read more, lines are too big
        int amount_read = ::read(fd, buffer + buffer_size, sizeof(buffer) - buffer_size);
        if (amount_read <= 0) goto done;  // read error or EOF
        buffer_size += amount_read;
        if (buffer_size > sizeof(buffer)) goto done;  // what the fuck?

    parse_line:
        umm newline_at = sizeof(buffer);
        for (umm i = 0; i < buffer_size; i++)
        {
            if (buffer[i] != '\n') continue;
            newline_at = i;
            break;
        }
        if (newline_at == sizeof(buffer))
            goto read_more;

        String line = { newline_at, buffer };

        String from_string = consume_until(&line, '-');
        String to_string   = consume_until(&line, ' ');
        String permissions = consume_until(&line, ' ');
        if (permissions.length == 4 && permissions[1] == 'w')
        {
            byte* from = (byte*)(umm)(u64_from_string(from_string, 16));
            byte* to   = (byte*)(umm)(u64_from_string(to_string,   16));
            umm   size = to - from;

            int prot = 0;
            CompileTimeAssert(PROT_NONE == 0);
            if (permissions[0] == 'r') prot |= PROT_READ;
            if (permissions[1] == 'w') prot |= PROT_WRITE;
            if (permissions[2] == 'x') prot |= PROT_EXEC;

            freeze_memory(user, from, to, prot);
        }

    next_line:
        buffer_size -= newline_at + 1;
        memmove(buffer, buffer + newline_at + 1, buffer_size);
        goto parse_line;
    }
    done:;
}

void exit_lockdown(User* user)
{
    assert(current_user == user);

    umm count = user->frozen_memory_count;
    while (user->frozen_memory_count)
    {
        umm i = --user->frozen_memory_count;
        mprotect(user->frozen_memory[i].base, user->frozen_memory[i].size, user->frozen_memory[i].prot);
    }

    ::signal(SIGSEGV, user->previous_sigsegv_handler);

    if (user->maps_fd >= 0)
        ::close(user->maps_fd);
    user->maps_fd = -1;

    current_user = NULL;
}


void set_most_recent_execution_location(struct User* user, Unit* unit, Bytecode const* bc)
{
    Block*     block = bc->generated_from_block;
    Expression expr  = bc->generated_from_expression;
    if (!block) return;

    Token_Info* info = get_token_info(unit->ctx, &block->from);
    if (expr != NO_EXPRESSION)
        info = get_token_info(unit->ctx, &block->parsed_expressions[expr].from);

    get_line(unit->ctx, info, &user->most_recently_executed_line, NULL, &user->most_recently_executed_file);
}

#else

struct User;

User* create_user() { return NULL; }
void delete_user(User* user) {};

byte* user_alloc(User* user, umm size, umm alignment) { return (byte*) malloc(size); }
void  user_free (User* user, void* base)              { return free(base);           }

void enter_lockdown(User* user) {}
void exit_lockdown(User* user) {}

void set_most_recent_execution_location(User* user, Unit* unit, Bytecode const* bc) {}

#endif


ExitApplicationNamespace
