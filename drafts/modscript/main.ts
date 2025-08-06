

import { readFileSync } from 'node:fs';

import { LooseParser } from 'acorn-loose';
import * as acorn from 'acorn';
//import { Func, System, Module } from './structure';

type PrimitiveType = 'bool' | 'uint8' | 'uint16' | 'uint32' | 'int8' | 'int16' | 'int32';
type VariableType = PrimitiveType | 'object' | 'reference';

const primitiveTypesSet = new Set(['bool', 'uint8', 'uint16', 'uint32', 'int8', 'int16', 'int32']);


export type Dictionary<T> = { [key: string]: T };


let options: acorn.Options = {
    allowAwaitOutsideFunction: true,
    ecmaVersion: 'latest',
    sourceType: 'module',
    allowReserved: true,
    checkPrivateFields: false,
    locations: true,
};

let tree = LooseParser.parse(readFileSync('experiments/test1.js', 'utf-8'), options);

class CompileError extends Error {
    constructor(message: string, public readonly node?: any) {
        super(message);
        this.name = 'CompileError';
        if (node && node.loc) {
            this.message += ` at line ${node.loc.start.line}, column ${node.loc.start.column}`;
        }
        if (node && node.node && node.node.loc) {
            this.message += ` at line ${node.node.loc.start.line}, column ${node.node.loc.start.column}`;
        }
    }
}

function ca(condition: boolean, message: string, node?: any): asserts condition {
    if (!condition) {
        throw new CompileError(message, node);
    }
}

function arrayToDictionary<T>(array: T[], keyFn: (item: T) => string): Dictionary<T> {
    let dict: Dictionary<T> = {};
    for (let item of array) {
        let key = keyFn(item);
        ca(!(key in dict), `Duplicate name '${key}' found.`, item);
        dict[key] = item;
    }
    return dict;
}

class AcornBased<T = acorn.Node> {
    node: T;
    constructor(node: T) {
        this.node = node;
    }
}

class System extends AcornBased<acorn.Program> {

    modules: Module[] = [];
    moduleByName: Dictionary<Module> = {};
    functions: Func[] = [];
    functionByName: Dictionary<Func> = {};

    constructor(node: acorn.Program) {
        super(node);
        for (let statement of node.body) {
            switch (statement.type) {
                case 'LabeledStatement': {
                    let mod = new Module(this, statement);
                    this.modules.push(mod);
                    this.functions.push(mod.func);
                    break;
                }
                case 'FunctionDeclaration':
                    this.functions.push(new Func(this, undefined, undefined, statement));
                    break;
                default:
                    throw new CompileError(`${statement.type} is not a valid top-level statement.`, statement);
            }
        }
        this.moduleByName = arrayToDictionary(this.modules, mod => mod.name);
        this.functionByName = arrayToDictionary(this.functions, func => func.name);
    }

    resolveTypes() {
        for (let func of this.functions) {
            func.resolveTypes();
        }
    }

    allocateVariables() {
        for (let func of this.functions) {
            func.allocateVariables();
        }
    }
}

class Module {

    name: string;
    func: Func;

    constructor(system: System, statement: acorn.LabeledStatement) {
        this.name = statement.label.name;
        ca(statement.body.type === 'BlockStatement', 'Expecting a block for module declaration.', statement.body);
        let dummyFunction: acorn.FunctionDeclaration = {
            type: 'FunctionDeclaration',
            id: { ...statement.label, name: `${this.name}@MOD_FUNCTION` },
            params: [],
            body: statement.body,
            generator: false,
            async: false,
            expression: false,
            loc: statement.loc,
            start: statement.start,
            end: statement.end,
        }
        this.func = new Func(system, this, undefined, dummyFunction);
    }
}

class Func {
    name: string;
    functions: Func[] = [];
    functionByName: Dictionary<Func> = {};
    variables: Variable[] = [];
    variableByName: Dictionary<Variable> = {};
    paramByName: Dictionary<Variable> = {};

    bodyStatements: Exclude<acorn.Statement, acorn.LabeledStatement | acorn.FunctionDeclaration>[] = [];

    memorySize: number = 0;
    memoryAlign: number = 1;
    memoryMap: Variable[] = [];

    private allocatingVariablesState: 'none' | 'allocating' | 'allocated' = 'none';

    constructor(
        public system: System,
        public module: Module | undefined,
        public parent: Func | undefined,
        statement: acorn.FunctionDeclaration
    ) {
        this.name = statement.generator ? `remote@${statement.id.name}` : statement.id.name;
        ca(!statement.async && !statement.expression, 'Invalid function', statement);
        this.functions = statement.body.body
            .filter(s => s.type === 'FunctionDeclaration')
            .map(s => new Func(this.system, module, this, s));
        this.functionByName = arrayToDictionary(this.functions, func => func.name);
        this.variables = statement.body.body
            .filter(s => s.type === 'LabeledStatement')
            .map(s => new Variable(this.system, this.module, this, s));
        this.variableByName = arrayToDictionary(this.variables, variable => variable.name);
        this.bodyStatements = statement.body.body.filter(s => s.type !== 'FunctionDeclaration' && s.type !== 'LabeledStatement');
        for (let p of statement.params) {
            ca(p.type === 'Identifier', 'Expecting an identifier for function parameter.', p);
            ca(!!this.variableByName[p.name], `Parameter '${p.name}' has no type declaration.`, p);
            let variable = this.variableByName[p.name];
            variable.isParam = true;
            variable.paramIndex = Object.keys(this.paramByName).length;
            this.paramByName[p.name] = variable;
        }
    }

    allocateVariables(): void {
        if (this.allocatingVariablesState === 'allocated') return;
        for (let func of this.functions) {
            func.allocateVariables();
        }
        ca(this.allocatingVariablesState === 'none', 'Recursive object constructor calls.', this);
        this.allocatingVariablesState = 'allocating';
        let bitOffset = 0;
        let sorted = this.variables.sort((a, b) => {
            if (a.isParam && !b.isParam) return -1;
            if (!a.isParam && b.isParam) return 1;
            let aAlign = a.getAlignBits();
            let bAlign = b.getAlignBits();
            return bAlign - aAlign;
        });
        for (let variable of sorted) {
            let size = variable.getSizeBits();
            let align = variable.getAlignBits();
            this.memoryAlign = Math.max(this.memoryAlign, (align + 7) >> 3);
            bitOffset = (bitOffset + align - 1) & ~(align - 1);
            variable.bitOffset = bitOffset;
            bitOffset += size;
        }
        this.memorySize = ((bitOffset + 7) >> 3);
        this.allocatingVariablesState = 'allocated';
    }


    resolveTypes() {
        for (let func of this.functions) {
            func.resolveTypes();
        }
        for (let variable of this.variables) {
            variable.resolveTypes();
        }
    }
}

class Variable extends AcornBased<acorn.LabeledStatement> {

    name: string;
    typeExpression: acorn.Expression;
    varType!: VariableType;
    classFunc: Func | undefined = undefined;
    isParam: boolean = false;
    paramIndex: number = 0;
    bitOffset: number = 0;

    constructor(
        public system: System,
        public module: Module | undefined,
        public func: Func,
        statement: acorn.LabeledStatement
    ) {
        super(statement);
        this.name = statement.label.name;
        ca(statement.body.type === 'ExpressionStatement', 'Expecting variable type', statement.body);
        this.typeExpression = statement.body.expression;
    }

    resolveTypes() {
        let exp = this.typeExpression;
        if (exp.type === 'Identifier') {
            if (primitiveTypesSet.has(exp.name)) {
                this.varType = exp.name as VariableType;
                return;
            } else {
                this.varType = 'reference';
                if (this.module && this.module.func.functionByName[exp.name]) {
                    this.classFunc = this.module.func.functionByName[exp.name];
                } else if (this.system.functionByName[exp.name]) {
                    this.classFunc = this.system.functionByName[exp.name];
                } else {
                    throw new CompileError(`Constructor '${exp.name}' not found.`, exp);
                }
                return;
            }
        } else if (exp.type === 'CallExpression' && exp.callee.type === 'Identifier') {
            ca(!this.isParam, 'Object constructor cannot be used in parameter.', exp);
            this.varType = 'object';
            if (this.module && this.module.func.functionByName[exp.callee.name]) {
                this.classFunc = this.module.func.functionByName[exp.callee.name];
            } else if (this.system.functionByName[exp.callee.name]) {
                this.classFunc = this.system.functionByName[exp.callee.name];
            } else {
                throw new CompileError(`Constructor '${exp.callee.name}' not found.`, exp);
            }
            return;
        }
        console.log(this.typeExpression);
        process.exit(1);
    }


    getSizeBits(): number {
        switch (this.varType) {
            case 'bool':
                return 1;
            case 'uint8':
            case 'int8':
                return 8;
            case 'uint16':
            case 'int16':
                return 16;
            case 'uint32':
            case 'int32':
            case 'reference':
                return 32;
            case 'object':
                this.classFunc!.allocateVariables();
                return 8 * this.classFunc!.memorySize;
        }
    }

    getAlignBits(): number {
        switch (this.varType) {
            case 'bool':
                return 1;
            case 'uint8':
            case 'int8':
                return 8;
            case 'uint16':
            case 'int16':
                return 16;
            case 'uint32':
            case 'int32':
            case 'reference':
                return 32;
            case 'object':
                this.classFunc!.allocateVariables();
                return 8 * this.classFunc!.memoryAlign;
        }
    }
}

function findFunction(start: Func, name: string, node: any): Func {
    if (start.name === name) {
        return start;
    }
    if (start.functionByName[name]) {
        return start.functionByName[name];
    }
    if (start.parent) {
        return findFunction(start.parent, name, node);
    }
    if (start.system.functionByName[name]) {
        return start.system.functionByName[name];
    }
    throw new CompileError(`Function '${name}' not found.`, node);
}

function dumpObj(obj: Func | System, ind: string = ''): void {
    for (let func of obj.functions) {
        func.allocateVariables();
        console.log(`${ind}Function: ${func.name} [${func.memorySize}] align: ${func.memoryAlign}`);
        dumpObj(func, ind + '  ');
    }
    if (obj instanceof System) {
        for (let module of obj.modules) {
            console.log(`${ind}Module: ${module.name} -> ${module.func.name}`);
        }
    } else {
        for (let variable of obj.variables) {
            let t = '';
            if (variable.varType === 'object' || variable.varType === 'reference') {
                t = variable.classFunc?.name ?? 'unknown';
            }
            if (variable.isParam) {
                t += ` param[${variable.paramIndex}]`;
            }
            console.log(`${ind}Variable: ${variable.name}: ${variable.varType} ${t} [${variable.getSizeBits()}] @${variable.bitOffset}, align: ${variable.getAlignBits()}   `);
        }
    }
}

let system = new System(tree);
system.resolveTypes();
system.allocateVariables();
dumpObj(system);

/*
Stages:
1. Parse and collect all identifiers
2. Resolve variable types
3. Allocate variables and parameters in memory
4. Resolve all identifiers and collect usage information
5. Generate code for each function


Identifiers scopes:
* System: module names, global functions
  * Function: function names, local variables


Functions types:
* Normal function:
    call:
        initialization
        execution
* Object constructor function:
    initialization:
        initialization
        execution
* Module main function:
    initialization:
        initialization
    update:
        execution


*/
