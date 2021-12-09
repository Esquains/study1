from torch.utils.data import MapDataPipe, functional_datapipe, DataChunk
from typing import List, Optional, Sized, TypeVar


T = TypeVar('T')


@functional_datapipe('batch')
class BatcherMapDataPipe(MapDataPipe[DataChunk]):
    r""" :class:`BatcherMapDataPipe`.

    Map DataPipe to create mini-batches of data. An outer dimension will be added as
    `batch_size` if `drop_last` is set to `True`, or `length % batch_size` for the
    last batch if `drop_last` is set to `False`.

    Args:
        datapipe: Iterable DataPipe being batched
        batch_size: The size of each batch
        drop_last: Option to drop the last batch if it's not full
    """
    datapipe: MapDataPipe
    batch_size: int
    drop_last: bool
    length: Optional[int]

    def __init__(self,
                 datapipe: MapDataPipe[T],
                 batch_size: int,
                 drop_last: bool = False,
                 wrapper_class=DataChunk,
                 ) -> None:
        assert batch_size > 0, "Batch size is required to be larger than 0!"
        super().__init__()
        self.datapipe = datapipe
        self.batch_size = batch_size
        self.drop_last = drop_last
        self.length = None
        self.wrapper_class = wrapper_class

    def __getitem__(self, index) -> DataChunk:
        batch: List = []
        indices = range(index * self.batch_size, (index + 1) * self.batch_size)
        try:
            for i in indices:
                batch.append(self.datapipe[i])
            return self.wrapper_class(batch)
        except IndexError:
            if not self.drop_last and len(batch) > 0:
                return self.wrapper_class(batch)
            else:
                raise IndexError(f"Index {index} is out of bound.")

    def __len__(self) -> int:
        if self.length is not None:
            return self.length
        if isinstance(self.datapipe, Sized):
            if self.drop_last:
                self.length = len(self.datapipe) // self.batch_size
            else:
                self.length = (len(self.datapipe) + self.batch_size - 1) // self.batch_size
            return self.length
        raise TypeError("{} instance doesn't have valid length".format(type(self).__name__))
