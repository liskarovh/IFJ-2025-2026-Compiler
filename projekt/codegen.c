/**
 * @authors Šimon Dufek (xdufeks00)

 * @file codegen.c
 * 
 * Code generator implementation using Syntactic tree
 * BUT FIT
 * IFJ24
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "codegen.h"
#include "error.h"
#include "string.h"


void generate_unary(generator gen, char * result, ASTNodePtr node);
void generate_binary(generator gen, char * result, ASTNodePtr node);
void generate_expression(generator gen, char * result, ASTNodePtr node);
void generate_function_call(generator gen, ASTNodePtr function);
void generate_assignment(generator gen, ASTNodePtr node);
void generate_declaration(generator gen, ASTNodePtr node);
void generate_if_statement(generator gen, ASTNodePtr node);
void generate_node(ASTNodePtr node, generator gen);
void init_code(generator gen, ASTBodyPtr syntree);
void generate_code(generator gen, ASTBodyPtr syntree);
void generate_function(generator gen, ASTNodePtr node);


void locToLable(string lable){
    string new = string_create(10);
    for(int i = 3; i <= lable->length; i++){
        string_append_char(new, lable->data[i]);
    }
    string_clear(lable);
    string_append_literal(lable, new->data);
    string_destroy(new);
}

enum arity get_op_arity(ast_expression_type type){
    switch(type){ //get number of variables needed
        case AST_ADD:
        case AST_SUB:
        case AST_MUL:
        case AST_DIV:
        case AST_EQUALS:
        case AST_NOT_EQUAL:
        case AST_TERNARY:
        case AST_LT:
        case AST_LE:
        case AST_GT:
        case AST_GE:
        case AST_AND:
        case AST_OR:
        case AST_CONCAT: //concat
            return ARITY_BINARY;
        case AST_NOT:
            return ARITY_UNARY;
        default:
            return ARITY_UNDEFINED;
    }
}

void create_gen(generator gen){
    gen->output = string_create(2048);
    gen->counter = 0;
}

void createframe(generator gen){
    string_append_literal(gen->output, "CREATEFRAME\n");
}
void pushframe(generator gen){
    string_append_literal(gen->output, "PUSHFRAME\n");
}
void popframe(generator gen){
    string_append_literal(gen->output, "POPFRAME\n");
}
void return_code(generator gen){
    string_append_literal(gen->output, "RETURN\n");
}
void fn_call(generator gen, char * fn_name){
    string_append_literal(gen->output, "CALL ");
    string_append_literal(gen->output, fn_name);
    string_append_literal(gen->output, "\n");
}
void label(generator gen, char * label){
    string_append_literal(gen->output, "LABEL ");
    string_append_literal(gen->output, label);
    string_append_literal(gen->output, "\n");
}
void jump(generator gen, char * label){
    string_append_literal(gen->output, "JUMP ");
    string_append_literal(gen->output, label);
    string_append_literal(gen->output, "\n");
}
void add_jumpifeq(generator gen, char * label, char * symb1, char * symb2){
    string_append_literal(gen->output, "JUMPIFEQ ");
    string_append_literal(gen->output, label);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, symb1);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, symb2);
    string_append_literal(gen->output, "\n");
}
void add_jumpifneq(generator gen, char * label, char * symb1, char * symb2){
    string_append_literal(gen->output, "JUMPIFNEQ ");
    string_append_literal(gen->output, label);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, symb1);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, symb2);
    string_append_literal(gen->output, "\n");
}
void push(generator gen, char * name){
    string_append_literal(gen->output, "PUSHS ");
    string_append_literal(gen->output, name);
    string_append_literal(gen->output, "\n");
}
void pop(generator gen, char * name){
    string_append_literal(gen->output, "POPS ");
    string_append_literal(gen->output, name);
    string_append_literal(gen->output, "\n");
}
void define_variable(generator gen, char * name){
    string_append_literal(gen->output, "DEFVAR ");
    string_append_literal(gen->output, name);
    string_append_literal(gen->output, "\n");
}
void move_var(generator gen, char * var1, char * var2){
    string_append_literal(gen->output, "MOVE ");
    string_append_literal(gen->output, var1);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, var2);
    string_append_literal(gen->output, "\n");
}
void binary_operation(generator gen, char * op, char * result, char * left, char * right){
    string_append_literal(gen->output, op);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, result);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, left);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, right);
    string_append_literal(gen->output, "\n");
}
void op_add(generator gen, char * result, char * left, char * right){
    binary_operation(gen, "ADD", result, left, right);
}
void op_sub(generator gen, char * result, char * left, char * right){
    binary_operation(gen, "SUB", result, left, right);
}
void op_mul(generator gen, char * result, char * left, char * right){
    binary_operation(gen, "MUL", result, left, right);
}
void op_div(generator gen, char * result, char * left, char * right){
    binary_operation(gen, "DIV", result, left, right);
}
void op_idiv(generator gen, char * result, char * left, char * right){
    binary_operation(gen, "IDIV", result, left, right);
}
void op_lt(generator gen, char * result, char * left, char * right){
    binary_operation(gen, "LT", result, left, right);
}
void op_gt(generator gen, char * result, char * left, char * right){
    binary_operation(gen, "GT", result, left, right);
}
void op_eq(generator gen, char * result, char * left, char * right){
    binary_operation(gen, "EQ", result, left, right);
}
void op_and(generator gen, char * result, char * left, char * right){
    binary_operation(gen, "AND", result, left, right);
}
void op_or(generator gen, char * result, char * left, char * right){
    binary_operation(gen, "OR", result, left, right);
}
void op_concat(generator gen, char * result, char * left, char * right){
    binary_operation(gen, "CONCAT", result, left, right);
}
void op_not(generator gen, char * result, char * op){
    string_append_literal(gen->output, "NOT ");
    string_append_literal(gen->output, result);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, op);
    string_append_literal(gen->output, "\n");
}
void ifj_read(generator gen, char * name, char * type){
    string_append_literal(gen->output, "READ ");
    string_append_literal(gen->output, name);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, type);
    string_append_literal(gen->output, "\n");
}
void ifj_write(generator gen, char * name){
    string_append_literal(gen->output, "WRITE ");
    string_append_literal(gen->output, name);
    string_append_literal(gen->output, "\n");
}
void ifj_strlen(generator gen, char * output, char * input){
    string_append_literal(gen->output, "STRLEN ");
    string_append_literal(gen->output, output);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, input);
    string_append_literal(gen->output, "\n");
}
void ifj_getchar(generator gen, char * output, char * input, char * position){
    string_append_literal(gen->output, "GATCHAR ");
    string_append_literal(gen->output, output);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, input);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, position);
    string_append_literal(gen->output, "\n");
}
void IFJ_setchar(generator gen, char * output, char * position, char * input){
    string_append_literal(gen->output, "SETCHAR ");
    string_append_literal(gen->output, output);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, position);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, input);
    string_append_literal(gen->output, "\n");
}
void IFJ_type(generator gen, char * output, char * input){
    string_append_literal(gen->output, "TYPE ");
    string_append_literal(gen->output, output);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, input);
    string_append_literal(gen->output, "\n");
}
void IFJ_float2int(generator gen, char * output, char * input){
    string_append_literal(gen->output, "FLOAT2INT ");
    string_append_literal(gen->output, output);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, input);
    string_append_literal(gen->output, "\n");
}
void IFJ_int2char(generator gen, char * output, char * input){
    string_append_literal(gen->output, "INT2CHAR ");
    string_append_literal(gen->output, output);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, input);
    string_append_literal(gen->output, "\n");
}
void IFJ_stri2int(generator gen, char * output, char * input){
    string_append_literal(gen->output, "STRING2INT ");
    string_append_literal(gen->output, output);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, input);
    string_append_literal(gen->output, "\n");
}
void IFJ_int2float(generator gen, char * output, char * input){
    string_append_literal(gen->output, "INT2FLOAT ");
    string_append_literal(gen->output, output);
    string_append_literal(gen->output, " ");
    string_append_literal(gen->output, input);
    string_append_literal(gen->output, "\n");
}
void exit_code(generator gen, char * code){
    string_append_literal(gen->output, "EXIT ");
    string_append_literal(gen->output, code);
    string_append_literal(gen->output, "\n");
}



void generate_unary(generator gen, char * result, ast_expression node){
    ast_expression expr;
    ast_expression_type operation;

    expr = node->unary_op->expression;
    operation = node->type;

    generate_expression(gen, "GF@tmp_op", expr);

    switch (operation) { //switch of all operators
        case AST_NOT:
            op_not(gen, result, "GF@tmp_op");
            break;
        default:
            break;
    }
}

void generate_binary(generator gen, char * result, ast_expression node){
    ast_expression left;
    ast_expression right;
    ast_expression_type operation;

    left = node->binary_op->left;
    right = node->binary_op->rigth;
    operator = node->type;
    

    generate_expression(gen, "GF@tmp_l", left); //left expression
    generate_expression(gen, "GF@tmp_r", right); //right expression

    switch (operation) { //switch of all operators
        case AST_ADD:
            op_add(gen, result, left->expression->value->value->data, right->expression->value->value->data);
            break;
        case AST_SUB:
            op_sub(gen, result, left->expression->value->value->data, right->expression->value->value->data);
            break;
        case AST_MUL:
            op_mul(gen, result, left->expression->value->value->data, right->expression->value->value->data);
            break;
        case AST_DIV:
            op_div(gen, result, left->expression->value->value->data, right->expression->value->value->data);
            break;
        case AST_LT:
            op_lt(gen, result, left->expression->value->value->data, right->expression->value->value->data);
            break;
        case AST_GT:
            op_gt(gen, result, left->expression->value->value->data, right->expression->value->value->data);
            break;
        case AST_EQ:
            op_eq(gen, result, left->expression->value->value->data, right->expression->value->value->data);
            break;
        case AST_AND:
            op_and(gen, result, left->expression->value->value->data, right->expression->value->value->data);
            break;
        case AST_OR:
            op_or(gen, result, left->expression->value->value->data, right->expression->value->value->data);
            break;
        case AST_LE:
            op_lt(gen, "GF@tmp1", left->expression->value->value->data, right->expression->value->value->data);
            op_eq(gen, "GF@tmp2", left->expression->value->value->data, right->expression->value->value->data);
            op_lt(gen, result, "GF@tmp1", "GF@tmp2");
            break;
        case AST_GE:
            op_gt(gen, "GF@tmp1", left->expression->value->value->data, right->expression->value->value->data);
            op_eq(gen, "GF@tmp2", left->expression->value->value->data, right->expression->value->value->data);
            op_lt(gen, result, "GF@tmp1", "GF@tmp2");
            break;
        case AST_CONCAT:
            op_concat(gen, result, left->expression->value->value->data, right->expression->value->value->data);
            break;
        default:
            break;
    }
}

void generate_expression(generator gen, char * result, ast_expression node){

    switch(get_op_arity(node->type)){
        case ARITY_UNARY:
            generate_unary(gen, result, node); //only one variable
            break;
        case ARITY_BINARY:
            generate_binary(gen, result, node); //two variables
            break;
        default:
            break;
    }
}

void generate_ifjfunction(generator gen, ast_node node, char* output){ 
    // TODO: rewrite, ifj function is part of function call
    switch(node->ifjFunction->type){
        case IFJ_FUNCTION_CHR:
            IFJ_int2char(gen, output, node->ifjFunction->param1->value->data);
            break;
        case IFJ_FUNCTION_CONCAT:
            op_concat(gen, output, node->ifjFunction->param1->value->data, node->ifjFunction->param2->value->data);
            break;
        case IFJ_FUNCTION_F2I:
            IFJ_float2int(gen, output->, node->ifjFunction->param1->value->data);
            break;
        case IFJ_FUNCTION_I2F:
            IFJ_int2float(gen, output->value->data, node->ifjFunction->param1->value->data);
            break;
        case IFJ_FUNCTION_LENGTH:
            ifj_strlen(gen, output->value->data, node->ifjFunction->param1->value->data);
            break;
        case IFJ_FUNCTION_ORD:
            ifj_getchar(gen, "GF@tmp_ifj", node->ifjFunction->param1->value->data, node->ifjFunction->param2->value->data);
            IFJ_stri2int(gen, output->value->data, "GF@tmp_ifj");
            break;
        case IFJ_FUNCTION_READF64:
            ifj_read(gen, output->value->data, "float");
            break;
        case IFJ_FUNCTION_READI32:
            ifj_read(gen, output->value->data, "int");
            break;
        case IFJ_FUNCTION_READSTR:
            ifj_read(gen, output->value->data, "string");
            break;
        case IFJ_FUNCTION_STRCMP:
            break;
        case IFJ_FUNCTION_STRING:
            move_var(gen, output->value->data, node->ifjFunction->param1->value->data);
            break;
        case IFJ_FUNCTION_SUBSTRING:
            break;
        case IFJ_FUNCTION_WRITE:
            ifj_write(gen, node->ifjFunction->param1->value->data);
            break;
    }
}

void generate_function_call(generator gen, asst_node node){
    // TODO: create parsing and checking for ifj functions
    // TODO: push in reverse order
    ast_function_call fun = node->function_call;
    ast_parameter param = fun->parameters;
    while(param != null){
        push(gen, param->name);
        param = param->next;
    }
    locToLable(fun->name);
    fn_call(gen, fun->name); //call function
}

void generate_function_return(generator gen, ast_node node){
    if(node->return_expr->identity) //string or number value
        move_var(gen, "GF@fn_ret", node->return_expr->identity->value);
    else if (node->return_expr->identifier)
        move_var(gen, "GF@fn_ret", node->return_expr->identifier->value);
    else
        generate_expression(gen, "GF@fn_ret", node->functionReturn->expression);
    popframe(gen);
    return_code(gen);
}

void generate_assignment(generator gen, ast_node node){
    string name = string_create(10);
    string_append_literal(name, node->assignment->name);
    string value = string_create(10);
    if(node->assignment->expression != NULL){
        if(node->assignment->value->identitfier){ //not expression
            if(node->assignment->expression->expression->type != AST_NIL){
                string_append_literal(value, node->assignment->value->identifier);
            }
            else{
                string_append_literal(value, "nil@nil");
            }
            move_var(gen, name->data, value->data); //move LF@var value
        }
        else if(node->assignment->value->identity){
            move_var(gen, node->assignment->value->identity->string_value, value->data);
        }
        else{ //expression
            generate_expression(gen, name->data, node->assignment->value); //expression
        }
    }   
    string_destroy(name);
    string_destroy(value);
}

void generate_declaration(generator gen, ast_node node){
    define_variable(gen, node->declaration->name);
    //generate_assignment(gen, node->declaration->assignment); //when implemented
}

void generate_if_statement(generator gen, ast_node node){
    char tmp[20];
    string end_label = string_create(20);
    string else_lable = string_create(20);
    ast_block body;
    ast_node body_node

    string_append_literal(end_label, "conditionEnd");
    snprintf(tmp, 20, "%u", gen->counter++);
    string_append_literal(end_label, tmp); //create one of label for end of if

    if(node->condition->else_branch == NULL) //no else
        string_append_literal(else_lable, end_label->data);
    else{
        string_clear(else_lable);
        string_append_literal(else_lable, "ifEnd");
        snprintf(tmp, 20, "%u", gen->counter);
        string_append_literal(else_lable, tmp); //create one of label for else
    }
    
    //create condition
    string_append_literal(gen->output, "\n# IF CONDITION\n");
    generate_expression(gen, "GF@tmp_if", node->condition->condition); //creates the condition
    add_jumpifeq(gen, else_lable->data, "GF@tmp_if", "bool@false");
    string_append_literal(gen->output, "# IF CONDITION END\n\n");

    
    if(node->condition->if_branch != NULL){
        string_append_literal(gen->output, "# IF BRANCH\n");
        body = node->condition->if_branch;
        body_node = body->first;
        while(body_node != NULL){
            generate_node(body_node, gen);
            body_node = body_node->next;
        }
        jump(gen, end_label->data);
    }
    if(node->condition->else_branch != NULL){ //else exists
        label(gen, else_lable->data);
        string_append_literal(gen->output, "\n# ELSE BRANCH\n");
        body = node->condition->else_branch;
        body_node = body->first;
        while(body_node != NULL){
            generate_node(body_node, gen);
            body_node = body_line->next;
        }
    }

    label(gen, end_label->data);
    string_append_literal(gen->output, "\n");
    string_destroy(end_label);
    string_destroy(else_lable);
}

void generate_while(generator gen, ast_node node){
    char tmp[20];
    string while_start = string_create(20);
    string while_end = string_create(20);
    ast_block body;
    ast_node body_node;

    string_append_literal(while_start, "whileStart");
    string_append_literal(while_end, "whileEnd");
    snprintf(tmp, 20, "%u", gen->counter++);
    string_append_literal(while_start, tmp); //create one of label for loop start
    string_append_literal(while_end, tmp); //create one of label for loop end

    
    string_append_literal(gen->output, "\n# WHILE LOOP START\n");
    
    generate_expression(gen, "GF@tmp_while", node->while_loop->condition); //creates the condition
    add_jumpifeq(gen, while_end->data, "GF@tmp_while", "bool@false");

    string_append_literal(gen->output, "\n");
    label(gen, while_start->data); //start lable


    body = node->while_loop->body;
    body_node = body->first;
    while(body_node != NULL){ //generate inner code
        generate_node(body_node, gen);
        body_node = body_node->next;
    }


    string_append_literal(gen->output, "\n");
    generate_expression(gen, "GF@tmp_while", node->while_loop->condition); //creates the condition
    add_jumpifneq(gen, while_start->data, "GF@tmp_while", "bool@false");

    label(gen, while_end->data); //end lable
    string_append_literal(gen->output, "# WHILE LOOP END\n\n");

    string_destroy(while_start);
    string_destroy(while_end);
}

void generate_node(ast_node node, generator gen){
    switch(node->type){
        case AST_CONDITION: // if statement
            generate_if_statement(gen, node);
            break;

        case AST_VAR_DECLARATION: //var declaration
            generate_declaration(gen, node);
            break;

        case AST_ASSIGNMENT: //var assignment
            generate_assignment(gen, node);
            break;

        case AST_IFJ_FUNCTION: //read / write
            generate_ifjfunction(gen, node, NULL);
            break;

        case AST_WHILE_LOOP:
            generate_while(gen, node);
            break;

        case AST_CALL_FUNCTION:
            generate_function_call(gen, node);
            break;

        case AST_RETURN:
            generate_function_return(gen, node);
            break;

        default:
            break;
    }
}

void init_code(generator gen, ast ast){ //.IFJcode25 on the first line and initiale all temp variables
    if(ast != NULL && ast->class_list != NULL){
        create_gen(gen);
        string_append_literal(gen->output, ".IFJcode25 \n\n");
        
        define_variable(gen, "GF@tmp_if");
        define_variable(gen, "GF@tmp_while");
        define_variable(gen, "GF@tmp_l");
        define_variable(gen, "GF@tmp_r");
        define_variable(gen, "GF@tmp_op");
        define_variable(gen, "GF@tmp_ifj");
        define_variable(gen, "GF@tmp1");
        define_variable(gen, "GF@tmp2");
        define_variable(gen, "GF@fn_ret");
    }
}

void generate_code(generator gen, ast ast){ //go threw all nodes in AST
    if(ast != NULL && ast->class_list != NULL){
        ast_class program = ast->class_list; //opens 'program' class   
        ast_block program_body = program->current; // class block
        ast_node fun = program_body->first; // function node
        while(fun != NULL){
            generate_function(gen, fun->function);
            fun = fun->next; // next node, must be function or null
        }
        string_append_literal(gen->output, "\n#END OF FILE\n");
    }
}

void generate_function(generator gen, ast_function fun){
    ast_block fun_body = fun->code;
    locToLable(fun->name);
    string_append_literal(gen->output, "\n# START OF FUNCTION ---");
    string_append_literal(gen->output, fun->name);
    string_append_literal(gen->output, "\n");
    label(gen, fun->name);
    createframe(gen); //create function frame
    pushframe(gen); //use new frame
    ast_parameter param = fun->parameters;
    while(param != null){
        define_variable(gen, param->name);
        pop(gen, param->name);
        param = param->next;
    }
    
    ast_node fun_node = fun_body->first;
    while(fun_node){ //generate all inside code
        generate_node(fun_node, gen);
        fun_node = fun_node->next;
    }
    popframe(gen); //pop frame from stack and use previous
    string_append_literal(gen->output, "# END OF FUNCTION ---");
    string_append_literal(gen->output, fun->name);
    string_append_literal(gen->output, "\n");
    if(strcmp(fun->name, "main") == 0)
        exit_code(gen, "int@0");
}