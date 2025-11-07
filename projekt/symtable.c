#include "symtable.h"


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
    place->data->defined = defined;
    table->size++;
    place->data->param_count = 0;
    

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

