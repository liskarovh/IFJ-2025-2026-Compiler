#include "symtable.h"
#include "string.h"


char *my_strdup(const char *s) {
    char *dup = malloc(strlen(s) + 1);
    if (dup) strcpy(dup, s);
    return dup;
}


unsigned st_hash(char *key) {
    unsigned long hash = 5381;
    int c;

    while ((c = *key++)) {
        hash = ((hash << 5) + hash) + c;
    }

    return (unsigned)(hash);
}


symtable *st_init(void){
    symtable *table = malloc(sizeof(symtable));
    if (table == NULL) return NULL;
    
    table->table = calloc(SYMTABLE_SIZE,sizeof(st_symbol));
    if(table->table == NULL){
        free(table);
        return NULL;
    }
    for (int i = 0; i < SYMTABLE_SIZE;i++){
        table->table[i].occupied = false;
        table->table[i].deleted = false;
        table->table[i].key = NULL;
        table->table[i].data = NULL;
    }
    
    table->size = 0;
    
    return table;
}

st_symbol *st_find(symtable *table, char *key){
    if (key == NULL) return NULL;

    unsigned index = st_hash(key) %SYMTABLE_SIZE;
    unsigned start = index;

    do{
        st_symbol *place = &table->table[index];

        if (place->occupied && strcmp(place->key, key) == 0) return place;

        index = (index +1) % SYMTABLE_SIZE;

    } while (index != start);

    return NULL;

}

void st_insert(symtable *table,char *key, symbol_type type, bool defined){
    st_symbol *current = st_find(table, key);

    if (current != NULL) return;

    unsigned index = st_hash(key) % SYMTABLE_SIZE;

    while (table->table[index].occupied){
        index = (index + 1) % SYMTABLE_SIZE;
    }

    st_symbol *place = &table->table[index];
    place->key = my_strdup(key);
    if(place->key == NULL) return ;

    place->data = malloc(sizeof(st_data));
    if( place->data == NULL) {
        free(place->key);
        return;
    }

    place->occupied = true;
    place->deleted = false;

    place->data->symbol_type = type;
    place->data->defined     = defined;
    place->data->param_count = 0;
    place->data->data_type   = ST_NULL;
    place->data->global      = false;
    place->data->ID          = NULL;
    place->data->params      = NULL;
    place->data->scope_name  = NULL;

    table->size++;
}

st_data *st_get(symtable *table, char *key){
    st_symbol *get = st_find(table, key);

    if (get == NULL) return NULL;

    return (get->data);

}

void st_free(symtable *table) {
    if (!table) return;

    for (int i = 0; i < SYMTABLE_SIZE; i++) {
        if (table->table[i].occupied) {
            free(table->table[i].key);
            free(table->table[i].data);
        }
    }
    free(table->table);
    free(table);
}

void st_dump(symtable *table, FILE *out) {
    if (!table || !out) return;

    fprintf(out, "-- symtable dump (size=%u, capacity=%d) --\n",
            table->size, SYMTABLE_SIZE);

    for (int i = 0; i < SYMTABLE_SIZE; i++) {
        st_symbol *s = &table->table[i];
        if (!s->occupied) continue;

        const char *key = s->key ? s->key : "(null)";
        st_data *data   = s->data;

        int kind  = data ? (int)data->symbol_type : -1;
        int arity = data ? data->param_count : -1;

        const char *acc  = NULL;   // "getter" | "setter" | NULL
        const char *base = NULL;
        if (key && strncmp(key, "get:", 4) == 0) { acc = "getter"; base = key + 4; }
        else if (key && strncmp(key, "set:", 4) == 0) { acc = "setter"; base = key + 4; }


        fprintf(out, "[%05d] key=%-24s kind=%d", i, key, kind);
        if (data && (data->symbol_type == ST_FUN ||
             data->symbol_type == ST_GETTER ||
             data->symbol_type == ST_SETTER)) {
           fprintf(out, " arity=%d", arity);
        }



        if (data && data->scope_name) {
            fputs(" scope=", out);
            if (data->scope_name->data && data->scope_name->length) {
                fwrite(data->scope_name->data, 1, data->scope_name->length, out);
            } else {
                fputs("(empty)", out);
            }
        }


        if (acc && base) {
            fprintf(out, " accessor=%s base=%s", acc, base);

            string val = NULL;
            if (data) {
                if (data->ID && data->ID->length) {
                    val = data->ID;
                } else if (data->param_count > 0 && data->params && data->params[0] && data->params[0]->length) {
                    val = data->params[0];
                }
            }
            if (val) {
                fputs(" value=", out);
                fwrite(val->data, 1, val->length, out);
            }
        }

        fputc('\n', out);
    }
    fprintf(out, "-- end dump --\n");
}


void st_foreach(symtable *table, st_iter_cb cb, void *user_data) {
    if (!table || !cb) return;

    for (int i = 0; i < SYMTABLE_SIZE; i++) {
        st_symbol *s = &table->table[i];
        if (!s->occupied || !s->key || !s->data) {
            continue;
        }
        cb(s->key, s->data, user_data);
    }
}
