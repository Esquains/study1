class Tensor_Type(type):
    """
    Tensor_Type defines a type for tensors, which consists of a list of dimensions.

    Example:
        class M(torch.nn.Module):
            def forward(self, x:Tensor_Type((1,2,3, Dyn)), y:Tensor_Type((1,2,3, Dyn))):
                return torch.add(x, y)
    """
    def __new__(cls, dim):
        return super().__new__(cls,f'Tensor_Type({dim})',  (Tensor_Type,), {})

    def __init__(self, dim):
        super().__init__(self,dim)
        self.__origin__ = Tensor_Type
        self.__args__ = dim

    def __repr__(self):
        return f'Tensor_Type[{self.__args__}]'

    def __eq__(self, other):
        if isinstance(other, self.__class__):
            return self.__args__ == other.__args__
        else:
            return False

    @staticmethod
    def __class_getitem__(*args):
        assert isinstance(args[0], tuple)
        return Tensor_Type(args[0])

class Dyn_Type:
    """
    Dyn_Type defines a type which stands for the absence of type information.
    """
    def __init__(self):
        self.__name__ = 'Dyn_Type'

    def __eq__(self, other):
        return isinstance(other, self.__class__)

Dyn = Dyn_Type()


def consistency(t1, t2):
    """
    A binary relation denoted by ~ that determines if t1 is consistent with t2.
    The relation is reflexive, semmetric but not transitive.
    returns True if t1 and t2 are consistent and False otherwise.

    Example:
        Dyn ~ Tensor_Type((1,2,3))
        int ~ Dyn
        int ~ int
        Tensor_Type((1,Dyn,3)) ~ Tensor_Type((1,2,3))
    """
    if t1 == t2:
        return True

    if isinstance(t1, Dyn_Type) or isinstance(t2, Dyn_Type):
        return True

    if isinstance(t1, Tensor_Type) and isinstance(t2, Tensor_Type):
        return len(t1.__args__) == len(t2.__args__) and \
               all([consistency(elem1, elem2) for elem1,elem2 in zip(t1.__args__, t2.__args__)])
