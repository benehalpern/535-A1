#include "../include/zcs.h"
#include "../include/multicast.h"
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define foreach(item, array)                                                   \
  for (int keep = 1, count = 0, size = sizeof(array) / sizeof(*(array));       \
       keep && count != size; keep = !keep, count++)                           \
    for (item = (array) + count; keep; keep = !keep)

#define MIN_TYPE_NUMBER 1
enum Msg_type {
  NOTIFICATION = MIN_TYPE_NUMBER,
  DISCOVERY,
  HEARTBEAT,
  AD,

  // ALWAYS KEEP THIS LAST
  MAX_MESSAGE_TYPE
};

enum Status { DOWN, UP };

int validate_message_type(int type) {
  return (type >= MIN_TYPE_NUMBER && type < MAX_MESSAGE_TYPE);
}

typedef struct _zcs_node_t {
  char *name;
  enum Status status;
  time_t hearbeat_time;
  zcs_cb_f cback;
  struct _zcs_node_t *next;
  zcs_attribute_t attributes[10];
} zcs_node_t;

typedef struct _node_list_t {
  zcs_node_t *head;
  zcs_node_t *tail;
} node_list_t;

typedef struct up_down_log {
  char log_entry[69];
  struct up_down_log *next;
} up_down_log_t;

const int MAX_LOG_SIZE = 50;
typedef struct _log_list {
  up_down_log_t *head;
  up_down_log_t *tail;
  int current_size;
} log_list_t;

typedef struct ad_notification {
  char *service_name;
  char *name;
  char *value;
} ad_notification_t;

mcast_t *m;
node_list_t *local_registry;
log_list_t *log_list;
char *service_name;
zcs_attribute_t *attribute_array;
int num_attr;
int STARTED = 0;
int INITIALIZED = 0;
int TYPE_OF_PROGRAM;

// Global var to stop thread
int stopThread = 0;

// Methods

void add_node(zcs_node_t *node) {
  if (local_registry->head == NULL) {
    local_registry->head = node;
    local_registry->tail = node;
  } else {
    local_registry->tail->next = node;
    local_registry->tail = node;
  }
}

void add_log(up_down_log_t *log) {
  if (log_list == NULL) {
    log_list = (log_list_t *)malloc(sizeof(log_list_t));
  }
  if (log_list->head == NULL) {
    log_list->head = log;
    log_list->tail = log;
    log_list->current_size = 1;
  } else {
    log_list->tail->next = log;
    log_list->tail = log;
    log_list->current_size++;

    if (log_list->current_size > MAX_LOG_SIZE) {
      up_down_log_t *to_delete = log_list->head;
      log_list->head = to_delete->next;
      free(to_delete->log_entry);
      free(to_delete);
    }
  }
}

void create_log(char log[69]) {
  up_down_log_t *log_object = (up_down_log_t *)malloc(sizeof(up_down_log_t));
  strncpy(log_object->log_entry, log, 69);

  add_log(log_object);
}

char *status_to_text(enum Status status) {
  switch (status) {
  case UP:
    return "UP";
  case DOWN:
    return "DOWN";
  default:
    exit(EXIT_FAILURE);
  }
}

void create_up_down_log(char *service_name, enum Status status) {
  char *status_log = status_to_text(status);
  char buffer[69];
  snprintf(buffer, 69, "%s: %s", service_name, status_log);
  create_log(buffer);
}

zcs_node_t *find_node_by_name(char *name) {
  if (name == NULL) {
    return NULL;
  }
  zcs_node_t *current = local_registry->head;
  while (current != NULL) {
    if (strcmp(current->name, name) == 0) {
      return current;
    }
    current = current->next;
  }
  return NULL;
}

void copy_array(const zcs_attribute_t given_attributes[],
                zcs_attribute_t **local_attribute_array, int num) {
  *local_attribute_array = malloc(num * sizeof(zcs_attribute_t));

  if (*local_attribute_array == NULL) {
    fprintf(stderr, "malloc failed\n");
    exit(EXIT_FAILURE);
  }

  memcpy(*local_attribute_array, given_attributes,
         num * sizeof(zcs_attribute_t));
}

void update_status(zcs_node_t *node, enum Status status) {
  enum Status old_status = node->status;
  if (old_status != status) {
    create_up_down_log(node->name, status);
    node->status = status;
  }
  if (status == UP) {
    node->hearbeat_time = time(NULL);
  }
}

char *create_discovery_msg() {
  size_t len = snprintf(NULL, 0, "%d#", DISCOVERY) + 1;

  char *result = malloc(len);

  snprintf(result, len, "%d#", DISCOVERY);

  return result;
}

char *create_heartbeat_msg() {
  size_t len = snprintf(NULL, 0, "%d#%s#", HEARTBEAT, service_name) + 1;

  char *result = malloc(len);

  snprintf(result, len, "%d#%s#", HEARTBEAT, service_name);

  return result;
}

char *create_notification_msg() {
  enum Msg_type type = NOTIFICATION;
  size_t header_len = snprintf(NULL, 0, "%d#%s#", type, service_name);
  header_len += 1;
  size_t total_len = header_len;

  for (int i = 0; i < num_attr; ++i) {
    total_len += snprintf(NULL, 0, "%s;%s#", attribute_array[i].attr_name,
                          attribute_array[i].value);
  }

  char *result = malloc(total_len);

  if (result == NULL) {
    fprintf(stderr, "Memory allocation failed!\n");
    exit(EXIT_FAILURE);
  }

  snprintf(result, header_len, "%d#%s#", type, service_name);

  for (int i = 0; i < num_attr; ++i) {
    strcat(result, attribute_array[i].attr_name);
    strcat(result, ";");
    strcat(result, attribute_array[i].value);
    strcat(result, "#");
  }

  return result;
}

char *create_ad_msg(char *ad_name, char *ad_value) {
  enum Msg_type type = AD;
  size_t len =
      snprintf(NULL, 0, "%d#%s#%s#%s#", type, service_name, ad_name, ad_value) +
      1;

  char *result = malloc(len);

  snprintf(result, len, "%d#%s#%s#%s#", type, service_name, ad_name, ad_value);

  return result;
}

void handle_notification(char *token) {
  if (TYPE_OF_PROGRAM != ZCS_APP_TYPE) {
    return;
  }
  token = strtok(NULL, "#");
  if (token == NULL) {
    // TODO: handle service name empty error
  }
  zcs_node_t *node = find_node_by_name(token);

  if (node != NULL) {
    update_status(node, UP);
    return;
  }

  node = (zcs_node_t *)malloc(sizeof(zcs_node_t));
  node->name = token;
  update_status(node, UP);

  token = strtok(NULL, "#");
  if (token == NULL) {
    // TODO: handle attributes empty
  }
  int i = 0;
  while (token != NULL) {
    char *kv_separator = strchr(token, ';');
    if (kv_separator == NULL) {
      // TODO: handle error
    }
    *kv_separator = '\0';

    strncpy(node->attributes[i].attr_name, token,
            sizeof(node->attributes[i].attr_name));
    strncpy(node->attributes[i].value, kv_separator + 1,
            sizeof(node->attributes[i].value));

    token = strtok(NULL, "#");
    i++;
  }
  add_node(node);
  return;
}

void handle_heartbeat(char *token) {
  if (TYPE_OF_PROGRAM != ZCS_APP_TYPE) {
    return;
  }
  token = strtok(NULL, "#");

  zcs_node_t *node = find_node_by_name(token);
  if (node == NULL)
    return;
  update_status(node, UP);
}

void handle_ad() {
  if (TYPE_OF_PROGRAM != ZCS_APP_TYPE) {
    return;
  }
  ad_notification_t *ad =
      (ad_notification_t *)malloc(sizeof(ad_notification_t));

  ad->service_name = strtok(NULL, "#");
  ad->name = strtok(NULL, "#");
  ad->value = strtok(NULL, "#");

  zcs_node_t *node = find_node_by_name(ad->service_name);

  if (node == NULL) {
    return;
  }

  zcs_cb_f callback = node->cback;
  if (callback == NULL)
    return;

  node->cback(ad->name, ad->value);
  return;
}

void handle_disc() {
  if (TYPE_OF_PROGRAM != ZCS_SERVICE_TYPE) {
    return;
  }

  char *notification = create_notification_msg();
  printf("Sending: '%s'\n", notification);
  printf("Size of notification: '%lu'\n", strlen(notification));
  multicast_send(m, notification, strlen(notification));
}

void handle_msg(char *msg) {
  printf("Received message: %s\n", msg);
  if (msg == NULL) {
    // TODO: handle error
  }

  char *token = strtok(msg, "#");

  if (token == NULL) {
    // TODO: handle invalid format error
  }

  int msg_type;
  if (sscanf(token, "%d", &msg_type) != 1 || !validate_message_type(msg_type)) {
    // TODO: handle errors
  }

  switch (msg_type) {
  case NOTIFICATION:
    handle_notification(token);
    break;
  case DISCOVERY:
    handle_disc();
    break;
  case AD:
    handle_ad();
    break;
  case HEARTBEAT:
    handle_heartbeat(token);
    break;
  default:
    // TODO: handle error
    break;
  }
}

/*
  Help functions
*/

void *run_receive_service_message() {
  while (1) {
    // Check for messages
    int rc = multicast_check_receive(m);

    // If there is a message, then process it
    if (rc > 0) {
      char *msg = (char *)malloc(sizeof(char) * 1024);
      // Receive the message

      multicast_receive(m, msg, 1024);
      handle_msg(msg);
    }
  }
  return 0;
}

/*
  Should this function return an int if for success or fail?
  Should the thread be stopped in this function?
*/
void *run_receive_discovery_message() {
  while (stopThread == 0) {
    // Continually check for DISCOVERY messages
    int rc = multicast_check_receive(m);
    if (rc > 0) {
      char *msg = (char *)malloc(sizeof(char) * 1024);
      multicast_receive(m, msg, 1024);

      handle_msg(msg);
    }
  }
  return 0;
}

/*
  Should this function return an int if for success or fail?
  Should the thread be stopped in this function?
*/
void *run_send_heartbeat() {
  while (stopThread == 0) {
    sleep(3);
    // Continually send HEARTBEAT messages
    char *heartbeat = create_heartbeat_msg();
    printf("Sending: '%s'\n", heartbeat);
    multicast_send(m, heartbeat, strlen(heartbeat));
  }
  return 0;
}

void *run_heartbeat_checker() {
  // Check the heartbeat count of all the nodes every 5 seconds

  while (1) {
    sleep(6);
    zcs_node_t *current = local_registry->head;
    while (current != NULL) {
      // If the node heartbeat count doesn't equal the required heartbeat count,
      // then set the status to DOWN
      if ((time(NULL) - current->hearbeat_time) > 3) {
        update_status(current, DOWN);
      }
      current = current->next;
    }
  }
  return 0;
}

/*
This initializes the ZCS library. This library function must be called before
issuing any other calls to the library. It sets up the parameters and performs
the initializations necessary for the library to work.
Returns a 0 if the initialization was a success.
Otherwise, it returns a -1.
 *
 */
int zcs_init(int type) {
  TYPE_OF_PROGRAM = type;

  pthread_t tid;

  // If the type is ZCS_APP_TYPE, then the node is an application
  if (TYPE_OF_PROGRAM == ZCS_APP_TYPE) {
    m = multicast_init("239.1.1.1", 5000, 8080);

    if (m == NULL) {
      return -1;
    }

    local_registry = malloc(sizeof(node_list_t));
    log_list = malloc(sizeof(log_list_t));

    // Support for receivig messages
    multicast_setup_recv(m);

    // Create thread to continually check for messages
    int result = pthread_create(&tid, NULL, run_receive_service_message, m);
    if (result != 0) {
      return -1;
    }

    int heartbeat_thread = pthread_create(&tid, NULL, run_heartbeat_checker, m);
    if (heartbeat_thread != 0) {
      return -1;
    }

    // Send a DISCOVERY message to the network
    char *disc_msg = create_discovery_msg();
    printf("Sending: '%s'\n", disc_msg);
    multicast_send(m, disc_msg, strlen(disc_msg));
  }
  // If the type is ZCS_SERVICE_TYPE, then the node is a discovery node
  else if (TYPE_OF_PROGRAM == ZCS_SERVICE_TYPE) {
    m = multicast_init("238.1.1.1", 8080, 5000);

    if (m == NULL) {
      return -1;
    }
    printf("Service created\n");
  }

  INITIALIZED = 1;
  return 0;
}

/*
This call puts the node online. The node has a name, and it is mandatory. It is
an ASCII string without spaces that is NULL terminated. It can have a maximum
length of 64 characters including the NULL termination. In addition to the name,
the node can have optional attributes. In practice, a node would be started with
at least one attribute (e.g., the node type). The attributes are specified as
key-value pairs and would remain unchanged until the node shuts down. The key
and value fields are ASCII characters. If you want to include integers, floats,
or other data types in the attribute values, they must be converted to ASCII
strings before specified as attributes. The last parameter specifies the number
of attributes passed into the node. Returns a 0 if the node start was a success.
Otherwise, it returns a -1, which happens if the start was attempted before the
initialization was called.
 */
int zcs_start(char *name, zcs_attribute_t attr[], int num) {
  // Check if the node was initialized
  if (INITIALIZED == 0) {
    return -1;
  }
  printf("Service started\n");

  int i = 0;
  while (name[i] != '\0') {
    i++;
  }
  if (i > 63) {
    return -1;
  }
  service_name = name;
  copy_array(attr, &attribute_array, num);
  num_attr = num;

  // Set up message receiving
  multicast_setup_recv(m);

  // Send a NOTIFICATION message to the network
  char *notification = create_notification_msg();
  printf("Sending: '%s'\n", notification);
  int sent = multicast_send(m, notification, strlen(notification));
  if (sent < 0) {
    return -1;
  }

  pthread_t tid1;
  pthread_t tid2;

  // Create a thread to run receive_discovery_message
  int result = pthread_create(&tid1, NULL, run_receive_discovery_message, m);

  if (result != 0) {
    return -1;
  }

  // Create a thread to run send_heartbeat
  int result2 = pthread_create(&tid2, NULL, run_send_heartbeat, m);

  if (result2 != 0) {
    return -1;
  }

  STARTED = 1;
  return 0;
}

/*
This is used to post an advertisement with the given name and value.
Advertisements are different from attributes. While attributes are node
properties that are valid until the node goes down, advertisements are messages
broadcasted by the node as soon as the above function is executed by the node.
The advertisement duration and repeat attempts
are pre-set in the ZCS library. The node will attempt to deliver the
advertisements to other nodes in the network according to the duration and
repeat attempts. Returns the number of times the advertisement was posted on the
network. It will return 0 (no posting) to indicate an error condition. This will
happen if the posting was called before the node was started.
  */
int zcs_post_ad(char *ad_name, char *ad_value) {
  // Send an ADD message to the network

  // Needs a bit of work to repeat attempts
  char *ad_msg = create_ad_msg(ad_name, ad_value);
  printf("Sending: '%s'\n", ad_msg);
  int sent = multicast_send(m, ad_msg, strlen(ad_msg));
  if (sent < 0) {
    return -1;
  }
  printf("Ad posted\n");
  return 0;
}

/*
This function is used to scan for nodes with a given value for a given
attribute. If no matching nodes are found, the call returns a 0. Otherwise, the
call returns the number of nodes found in the network. The names of the nodes
found are stored in the node_names. A call to zcs_query() can fail to find a
matching node if there are no nodes with matching attributes or the calling node
is not in the same network as the matching node.
*/
int zcs_query(char *attr_name, char *attr_value, char *node_names[],
              int namelen) {
  int i = 0;
  zcs_node_t *current = local_registry->head;
  while (current != NULL && i < namelen) {
    if (strcmp(current->attributes[i].attr_name, attr_name) == 0 &&
        strcmp(current->attributes[i].value, attr_value) == 0) {
      node_names[i] = current->name;
      i++;
    }
    current = current->next;
  }
  return i;
}

/*
This function is used to get the full list of attributes of a node that is
returned by the zcs_query() function. The first argument is the name of the
node. The second argument is an attribute array that is already allocated. The
third argument is set to the number of slots allocated in the attribute array.
The function sets it to the number of actual attributes read from the node. The
return value of the function is 0 if there is no error and is -1 if there is an
error.
*/
int zcs_get_attribs(char *name, zcs_attribute_t attr[], int *num) {
  zcs_node_t *current = local_registry->head;
  while (current != NULL) {
    if (strcmp(current->name, name) == 0) {
      for (int i = 0; i < *num; i++) {
        attr[i] = current->attributes[i];
      }
      return 0;
    }
    current = current->next;
  }
  return -1;
}

/*
This function takes two arguments. The first is a name of the target node and
the second is the callback that will be triggered when the target posts an
advertisement. The callback has two arguments: name of the advertisement and the
value of the advertisement. There is no mechanism for un-listening to an
advertisement.
*/
int zcs_listen_ad(char *name, zcs_cb_f cback) {
  zcs_node_t *current = local_registry->head;
  while (current != NULL) {
    if (strcmp(current->name, name) == 0) {
      current->cback = cback;
      return 0;
    }
    current = current->next;
  }
  return -1;
}

/*
This function is called to terminate the activities of the ZCS by a program
before it terminates. The call returns a 0 if it is a success. Otherwise, it
will return a -1. For example, if the call to shutdown is made before the node
was started it will return a -1.
*/
int zcs_shutdown() {
  // Check if the node was started
  if (STARTED == 0) {
    return -1;
  }

  // Close the multicast socket
  multicast_destroy(m);

  // Set global var so that the receive thread can stop
  stopThread = 1;

  // Free memory
  free(local_registry);

  printf("Service shut down\n");

  return 0;
}

/*
This function prints the node UP and DOWN logs. That is, every time a node fails
(goes down) the observing node makes a note of that event in its log. Similarly,
every time a node boots up (comes up) the observing node makes a note. This
function prints the log that is maintained at the local node. The log can be
tcloseruncated once it reaches a predefined length in size or time. There is no
return value for this function.
*/
void zcs_log() {
  up_down_log_t *current = log_list->head;
  while (current != NULL) {
    printf("%s\n", current->log_entry);
    current = current->next;
  }
  // fflush(stdout);
}
