#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <fcntl.h> 
#include <errno.h>

extern int errno;

// Doubly linked list node containing a word and its length
typedef struct word_node_t
{
    char *word;
    int length;
    struct word_node_t *prev;
    struct word_node_t *next;
} word_node_t;

// Function prototype
void word_wrap(int, int, word_node_t *);
void get_words(int, int, int);
bool duplicate_whitespace(word_node_t *, char);
word_node_t *new_node(char *, int);
void create_insert_word(int, char *, word_node_t **, word_node_t **);
void insert_at_end(word_node_t **, word_node_t **, char *, int);
void free_list(word_node_t *);
void clean_list(word_node_t **, bool);
void fix_spacing(word_node_t **);

int main(int argc, char **argv)
{
    // Checking if the arguments match up
    if (argc < 2 || argc > 3)
    {
        errno = EINVAL;
        perror("Error: Invalid number of arguments");
        return EXIT_FAILURE;
    }
    // Checking for null arguments
    if (argv[1] == NULL)
    {
        errno = EINVAL;
        perror("Error: Null width argument");
        return EXIT_FAILURE;
    }
    // Checking width argument correctness
    int width = atoi(argv[1]);
    if (width < 0)
    {
        errno = EINVAL;
        perror("Error: Width must be a non-zero positive integer");
        return EXIT_FAILURE;
    }
 
    // Check if the input is a folder
    struct stat desc;
    stat(argv[2], &desc);

    // If its a directory, do directory stuff :)
    if (S_ISDIR(desc.st_mode))
    {
        DIR *dir;
        struct dirent *file;

        if (!(dir = opendir(argv[2])))
        {
            errno = EACCES;
            perror("Error: Directory cannot be opened");
            return EXIT_FAILURE;
        }
        while ((file = readdir(dir)))
        {
            char *filename = file->d_name;

            // Skip dot files and files beginning with "wrap."
            if (strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0)
                continue;
            else if (strstr(filename, "wrap.") != 0)
                continue;
            else
            {
                // Create a new file for the wrapped text
                int full_path_size = strlen(argv[2]) + strlen("/") + strlen(filename) + 1;
                char *full_path = malloc(full_path_size * sizeof(char));

                // there's a shitton of strings being made
                // just bear with me :(
                strcpy(full_path, argv[2]);
                strcat(full_path, "/");
                strcat(full_path, filename);

                // File descriptor of input file
                int in_fd = open(full_path, O_RDWR);

                int new_filename_size = strlen("wrap.") + strlen(filename) + 1;
                char *new_filename = malloc(new_filename_size * sizeof(char));
                
                strcpy(new_filename, "wrap.");
                strcat(new_filename, filename);

                int new_full_path_size = strlen(argv[2]) + strlen("/") + strlen(new_filename) + 1;
                char *new_full_path = malloc(new_full_path_size * sizeof(char));

                strcpy(new_full_path, argv[2]);
                strcat(new_full_path, "/");
                strcat(new_full_path, new_filename);

                // File descriptor of output file
                int out_fd = open(new_full_path, O_CREAT | O_WRONLY, S_IRUSR);
                get_words(in_fd, out_fd, width);

                close(in_fd);
                close(out_fd);

                free(full_path);
                free(new_filename);
                free(new_full_path);
            }
        }
        closedir(dir);
    }
    else
    {
        // Otherwise we're dealing with files
        int fd;
        if (argv[2] == NULL)
            fd = 0;
        else
            fd = open(argv[2], O_RDWR);
        // Ensure that file exists -- otherwise report error
        if (fd == -1)
        {
            errno = EACCES;
            perror("Error: File does not exist or cannot be opened");
            return EXIT_FAILURE;
        }

        get_words(fd, 1, width);
        close(fd);
    }

    return EXIT_SUCCESS;
}

void word_wrap(int out_fd, int width, word_node_t *head)
{
    // Wrap to width
    int curr_line_width = 0;
    bool newline_printed = false;

    for (word_node_t *ptr = head; ptr != NULL; ptr = ptr->next)
    {
        if (strcmp(ptr->word, "\n\n") == 0)
        {
            write(out_fd, ptr->word, ptr->length);
            newline_printed = true;
            curr_line_width = 0;
        }
        else if (strcmp(ptr->word, " ") == 0)
        {
            // Do not begin lines with a space
            if (curr_line_width == 0)
                continue;
            // first check if next word would be able to fit
            // on the same line
            if (ptr->next != NULL)
            {
                if (ptr->next->length + 1 + curr_line_width <= width)
                    write(out_fd, ptr->word, ptr->length);
                // otherwise, do not print the space amd just move on
                else
                {
                    write(out_fd, "\n", strlen("\n"));
                    curr_line_width = 0;
                    continue;
                }
            }
        }
        // Add a regular word or space to the line
        else if (curr_line_width + ptr->length <= width)
        {
            write(out_fd, ptr->word, ptr->length);
            curr_line_width += ptr->length;
        }
        // Word will not fit on a line and needs its own line
        else if (ptr->length > width)
        {
            if (ptr->prev != NULL)
            {
                if (ptr->prev->length > width)
                {
                    write(out_fd, ptr->word, ptr->length);
                    write(out_fd, "\n", strlen("\n"));
                    newline_printed = true;
                }
                else
                {
                    if (!newline_printed)
                        write(out_fd, "\n", strlen("\n"));
                    write(out_fd, ptr->word, ptr->length);
                    newline_printed = false;
                }
            }
            else
            {
                write(out_fd, ptr->word, ptr->length);
                write(out_fd, "\n", strlen("\n"));
            }
            curr_line_width = 0;
        }
        // word is too big for the current line
        else
        {
            curr_line_width = 0;
            write(out_fd, ptr->word, ptr->length);
            curr_line_width += ptr->length;
        }
    }
}

// Makes a linked list containing all words in the file
void get_words(int in_fd, int out_fd, int width)
{
    // Keep track of first and last nodes of the list
    word_node_t *head = NULL, *last = NULL;

    // File not found -- read from stdin
    if (in_fd == -1)
    {
        printf("File does not exist\n");
        return;
    }
    
    // Keep track of how large our string array is
    // Add 1 to the width to account for the null terminator
    int max_word_size = width + 1, curr_word_size = 0;
    char *word = malloc(max_word_size * sizeof(char));
    
    // Current character seen, and current size of the word (if any)
    char curr;
    int has_chars, newlines = 0;

    // go through the file byte-by-byte 
    while ( (has_chars = read(in_fd, &curr, 1)) && has_chars != 0)
    {
        if (curr == ' ')
        {
            newlines = 0;

            // We should not be adding words with nothing in them
            if (curr_word_size != 0)
            {
                word[curr_word_size] = '\0';
                create_insert_word(curr_word_size, word, &head, &last);
                
                // Reset word buffer
                memset(word, 0, strlen(word));
                curr_word_size = 0;
                
                // If whitespace hasnt been encountered yet, we've reached a new "word"
                if (!duplicate_whitespace(last, curr))
                    insert_at_end(&head, &last, " ", 1);
            }
        }
        else if (curr == '\n')
        {
            newlines++;

            // If double new line has not been encountered yet
            if (newlines == 1)
            {
                if (strlen(word) != 0)
                {
                    // Add current word to the linked list
                    word[curr_word_size] = '\0';
                    create_insert_word(curr_word_size, word, &head, &last);

                    // clear out the word array now
                    memset(word, 0, strlen(word));
                    curr_word_size = 0;
                }
            }
            else if (newlines == 2)
            {                
                // If a double newline is following spaces,
                // replace the space w/ the double newlines
                if (strcmp(last->word, " ") == 0)
                    last->word = "\n\n";
                if (!duplicate_whitespace(last, curr))
                    insert_at_end(&head, &last, "\n\n", 2);
            }
            // Reset the count -- multiple newlines are being encountered
            else
                newlines = 0;
        }
        else
        {
            newlines = 0;
            
            // make more space if needed
            // leave 1 space for null terminator
            if (curr_word_size + 1 == max_word_size - 1)
            {
                max_word_size *= max_word_size;
                word = realloc(word, max_word_size);
            }

            word[curr_word_size++] = curr;
        }
    }

    // Get any last words
    if (curr_word_size != 0)
    {
        word[curr_word_size] = '\0';
        create_insert_word(curr_word_size, word, &head, &last);
    }

    // Clean whitespace at the beginning or end of the list
    bool is_head = true;
    clean_list(&head, is_head);
    clean_list(&last, !is_head);

    // Fix spacing
    fix_spacing(&head);

    word_wrap(out_fd, width, head);
    free_list(head);
    free(word);
}

// Checks if any works do not have spaces between them, if so add a space
void fix_spacing(word_node_t **node)
{
    for (word_node_t *ptr = *node; ptr != NULL; ptr = ptr->next)
    {
        // Ignore whitespace
        if (strcmp(ptr->word, " ") == 0 || strcmp(ptr->word, "\n\n") == 0)
            continue;
        // Ensure there is a next pointer
        else if (ptr->next != NULL)
        {
            char *curr_word = ptr->next->word;
            // If word is not followed by double newlines or a space
            // add a space
            if (strcmp(curr_word, " ") != 0 && strcmp(curr_word, "\n\n") != 0)
            {
                // Create new node
                word_node_t *temp = new_node(" ", 1);
                temp->prev = ptr;
                temp->next = ptr->next;

                // lorem->ipsum-> ->dookie-> ->daka
                // lorem<-temp( )->ipsum
                // lorem->temp( )->ipsum
                // Have current node point to new node
                ptr->next = temp;

                // Move on to next node to prevent infinite loops
                ptr = ptr->next;
            }
        }  
    }
}

// Clears out whitespace at beginning or end of the list
void clean_list(word_node_t **node, bool is_head)
{
    // Clear out whitespace at the head
    while (*node != NULL)
    {
        if (strcmp((*node)->word, " ") == 0 || strcmp((*node)->word, "\n\n") == 0)
        {
            word_node_t *temp = *node;
            
            // Move head forward, or tail backwards
            if (is_head)
            {
                *node = is_head ? (*node)->next : (*node)->prev;
                (*node)->prev = NULL;
            }
            else
            {
                *node = (*node)->prev;
                (*node)->next = NULL;
            }

            // then free
            // We don't need to free the word since " " and "\n\n"
            // are not allocated using malloc
            free(temp);
        }
        else
            break;
    }
}

// Creates a duplicate of current word and adds it to the end of the linked list of words
void create_insert_word(int curr_word_size, char *word, word_node_t **head, word_node_t **last)
{
    char *dupe = malloc((curr_word_size + 1) * sizeof(char));
    strcpy(dupe, word);
    insert_at_end(head, last, dupe, strlen(dupe));
}

// Check if last node contains a whitespace sequence already
bool duplicate_whitespace(word_node_t *last, char curr)
{
    if (last == NULL)
        return false;
    if (strcmp(last->word, "\n\n") == 0 || strcmp(last->word, " ") == 0)
        return true;
    return false;
}

// Create a new node
word_node_t *new_node(char *word, int length)
{
    word_node_t *new = malloc(sizeof(word_node_t));

    new->word = word;
    new->length = length;
    new->prev = NULL;
    new->next = NULL;

    return new;
}

// Inserts the new node at the end of the linked list
void insert_at_end(word_node_t **head, word_node_t **last, char *word, int length)
{
    if (*head == NULL)
    {
        *head = new_node(word, length);
        *last = *head;
    }
    else
    {
        (*last)->next = new_node(word, length);
        (*last)->next->prev = (*last);
        (*last) = (*last)->next;
    }
}

// Free entire linked list
void free_list(word_node_t *head)
{
    word_node_t *ptr = head;
    while (ptr != NULL)
    {
        if (strcmp(ptr->word, " ") != 0 && strcmp(ptr->word, "\n\n") != 0)
            free(ptr->word);
        word_node_t *temp = ptr->next;
        free(ptr);
        ptr = temp;
    }
}